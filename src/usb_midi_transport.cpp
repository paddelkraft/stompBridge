#include "usb_midi_transport.h"

#include <cstring>

#include "esp_err.h"
#include "soc/usb_struct.h"
#include "usb/usb_helpers.h"

namespace {
extern "C" void USBHostBoardInit(void);

bool allowUsbDevice(const usb_device_desc_t *, uint8_t *configuration) {
  if (configuration != nullptr) {
    *configuration = 1;
  }
  return true;
}

void clientEventCallback(const usb_host_client_event_msg_t *event,
                         void *context) {
  if (event != nullptr && context != nullptr) {
    static_cast<UsbMidiTransport *>(context)->onClientEvent(*event);
  }
}

void receiveCallback(usb_transfer_t *transfer) {
  if (transfer != nullptr && transfer->context != nullptr) {
    static_cast<UsbMidiTransport *>(transfer->context)
        ->onReceiveComplete(transfer);
  }
}

void transmitCallback(usb_transfer_t *transfer) {
  if (transfer != nullptr && transfer->context != nullptr) {
    static_cast<UsbMidiTransport *>(transfer->context)
        ->onTransmitComplete(transfer);
  }
}

uint8_t usbMidiPayloadLength(uint8_t cin) {
  static constexpr uint8_t kLengths[16] = {
      0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 1};
  return kLengths[cin & 0x0f];
}
} // namespace

bool UsbMidiTransport::begin() {
  if (started_) {
    return true;
  }
  USBHostBoardInit();

  usb_host_config_t hostConfig = {};
  hostConfig.intr_flags = ESP_INTR_FLAG_LEVEL1;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
  hostConfig.enum_filter_cb = allowUsbDevice;
#endif
  esp_err_t error = usb_host_install(&hostConfig);
  if (error != ESP_OK) {
    Serial.printf("USB host install failed: %s\n", esp_err_to_name(error));
    return false;
  }

  usb_host_client_config_t clientConfig = {};
  clientConfig.is_synchronous = false;
  clientConfig.max_num_event_msg = 8;
  clientConfig.async.client_event_callback = clientEventCallback;
  clientConfig.async.callback_arg = this;
  error = usb_host_client_register(&clientConfig, &client_);
  if (error != ESP_OK) {
    Serial.printf("USB host client registration failed: %s\n",
                  esp_err_to_name(error));
    return false;
  }

  if (xTaskCreate(daemonTask, "usb-daemon", 4096, this, 2, &daemonTask_) !=
          pdPASS ||
      xTaskCreate(clientTask, "usb-client", 6144, this, 3, &clientTask_) !=
          pdPASS) {
    Serial.println("USB host task creation failed");
    return false;
  }
  started_ = true;
  Serial.println("USB-MIDI host ready (ESP-IDF hub support)");
  return true;
}

void UsbMidiTransport::daemonTask(void *) {
  while (true) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
  }
}

void UsbMidiTransport::clientTask(void *context) {
  auto *transport = static_cast<UsbMidiTransport *>(context);
  while (true) {
    usb_host_client_handle_events(transport->client_, portMAX_DELAY);
  }
}

void UsbMidiTransport::poll() {
  const uint32_t port = USB0.hprt;
  if (port != lastPortStatus_) {
    lastPortStatus_ = port;
    Serial.printf(
        "USB root port: connected=%u enabled=%u line=%u power=%u speed=%u "
        "(HPRT=%08lX)\n",
        static_cast<unsigned>(port & 1u),
        static_cast<unsigned>((port >> 2) & 1u),
        static_cast<unsigned>((port >> 10) & 3u),
        static_cast<unsigned>((port >> 12) & 1u),
        static_cast<unsigned>((port >> 17) & 3u),
        static_cast<unsigned long>(port));
  }
}

bool UsbMidiTransport::connected() const {
  portENTER_CRITICAL(&stateMux_);
  const bool value = connected_;
  portEXIT_CRITICAL(&stateMux_);
  return value;
}

void UsbMidiTransport::onClientEvent(
    const usb_host_client_event_msg_t &event) {
  if (event.event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    openMidiDevice(event.new_dev.address);
    return;
  }
  if (event.event != USB_HOST_CLIENT_EVENT_DEV_GONE || device_ == nullptr ||
      event.dev_gone.dev_hdl != device_) {
    return;
  }

  portENTER_CRITICAL(&stateMux_);
  connected_ = false;
  txBusy_ = false;
  receivingSysEx_ = false;
  receiveLength_ = 0;
  portEXIT_CRITICAL(&stateMux_);
  usb_host_device_close(client_, device_);
  device_ = nullptr;
  Serial.println("USB-MIDI disconnected");
}

bool UsbMidiTransport::openMidiDevice(uint8_t address) {
  usb_device_handle_t candidate = nullptr;
  if (usb_host_device_open(client_, address, &candidate) != ESP_OK) {
    return false;
  }

  const usb_device_desc_t *descriptor = nullptr;
  if (usb_host_get_device_descriptor(candidate, &descriptor) != ESP_OK ||
      descriptor == nullptr) {
    usb_host_device_close(client_, candidate);
    return false;
  }
  Serial.printf("USB device enumerated: %04X:%04X\n", descriptor->idVendor,
                descriptor->idProduct);
  if (descriptor->idVendor != kGp5Vendor ||
      (descriptor->idProduct != kGp5Product &&
       descriptor->idProduct != kPocketMasterProduct)) {
    usb_host_device_close(client_, candidate);
    return false;
  }

  if (!discoverMidiInterface(candidate)) {
    Serial.println("USB device has no usable MIDI streaming interface");
    usb_host_device_close(client_, candidate);
    return false;
  }

  esp_err_t error = usb_host_interface_claim(client_, candidate,
                                              midiInterface_, 0);
  if (error != ESP_OK) {
    Serial.printf("GP-5 MIDI interface claim failed: %s\n",
                  esp_err_to_name(error));
    usb_host_device_close(client_, candidate);
    return false;
  }

  if (rxTransfer_ == nullptr &&
      usb_host_transfer_alloc(64, 0, &rxTransfer_) != ESP_OK) {
    Serial.println("GP-5 MIDI receive allocation failed");
    return false;
  }
  if (txTransfer_ == nullptr &&
      usb_host_transfer_alloc(128, 0, &txTransfer_) != ESP_OK) {
    Serial.println("GP-5 MIDI transmit allocation failed");
    return false;
  }

  device_ = candidate;
  rxTransfer_->device_handle = device_;
  rxTransfer_->bEndpointAddress = midiEndpointIn_;
  rxTransfer_->num_bytes = 64;
  rxTransfer_->callback = receiveCallback;
  rxTransfer_->context = this;
  error = usb_host_transfer_submit(rxTransfer_);
  if (error != ESP_OK) {
    Serial.printf("GP-5 MIDI receive start failed: %s\n",
                  esp_err_to_name(error));
    return false;
  }

  portENTER_CRITICAL(&stateMux_);
  connected_ = true;
  portEXIT_CRITICAL(&stateMux_);
  Serial.printf("USB-MIDI connected: %04X:%04X\n", descriptor->idVendor,
                descriptor->idProduct);
  return true;
}

bool UsbMidiTransport::discoverMidiInterface(usb_device_handle_t device) {
  const usb_config_desc_t *configuration = nullptr;
  if (usb_host_get_active_config_descriptor(device, &configuration) != ESP_OK ||
      configuration == nullptr) {
    return false;
  }
  for (uint8_t interfaceNumber = 0; interfaceNumber < 16;
       ++interfaceNumber) {
    int interfaceOffset = 0;
    const usb_intf_desc_t *interface = usb_parse_interface_descriptor(
        configuration, interfaceNumber, 0, &interfaceOffset);
    if (interface == nullptr || interface->bInterfaceClass != USB_CLASS_AUDIO ||
        interface->bInterfaceSubClass != 3) {
      continue;
    }
    uint8_t endpointIn = 0;
    uint8_t endpointOut = 0;
    for (uint8_t index = 0; index < interface->bNumEndpoints; ++index) {
      int endpointOffset = interfaceOffset;
      const usb_ep_desc_t *endpoint = usb_parse_endpoint_descriptor_by_index(
          interface, index, configuration->wTotalLength, &endpointOffset);
      if (endpoint == nullptr || (endpoint->bmAttributes & 0x03) != 0x02) {
        continue;
      }
      if ((endpoint->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) !=
          0) {
        endpointIn = endpoint->bEndpointAddress;
      } else {
        endpointOut = endpoint->bEndpointAddress;
      }
    }
    if (endpointIn != 0 && endpointOut != 0) {
      midiInterface_ = interface->bInterfaceNumber;
      midiEndpointIn_ = endpointIn;
      midiEndpointOut_ = endpointOut;
      Serial.printf("USB-MIDI interface %u, endpoints OUT %02X IN %02X\n",
                    static_cast<unsigned>(midiInterface_),
                    static_cast<unsigned>(midiEndpointOut_),
                    static_cast<unsigned>(midiEndpointIn_));
      return true;
    }
  }
  return false;
}

bool UsbMidiTransport::send(const uint8_t *midi, size_t length) {
  if (midi == nullptr || length == 0 || txTransfer_ == nullptr) {
    return false;
  }

  portENTER_CRITICAL(&stateMux_);
  const bool available = connected_ && !txBusy_;
  if (available) {
    txBusy_ = true;
  }
  portEXIT_CRITICAL(&stateMux_);
  if (!available) {
    return false;
  }

  size_t outputLength = 0;
  uint8_t *output = txTransfer_->data_buffer;
  if ((midi[0] & 0xf0) == 0xb0 && length == 3) {
    output[0] = 0x0b;
    memcpy(output + 1, midi, 3);
    outputLength = 4;
  } else if (midi[0] == 0xf0) {
    size_t offset = 0;
    while (offset < length) {
      const size_t remaining = length - offset;
      const size_t count = remaining > 3 ? 3 : remaining;
      uint8_t cin = 0x04;
      if (remaining <= 3 && midi[offset + count - 1] == 0xf7) {
        cin = count == 1 ? 0x05 : count == 2 ? 0x06 : 0x07;
      }
      if (outputLength + 4 > txTransfer_->data_buffer_size) {
        portENTER_CRITICAL(&stateMux_);
        txBusy_ = false;
        portEXIT_CRITICAL(&stateMux_);
        return false;
      }
      output[outputLength++] = cin;
      for (size_t byte = 0; byte < 3; ++byte) {
        output[outputLength++] = byte < count ? midi[offset + byte] : 0;
      }
      offset += count;
    }
  } else {
    portENTER_CRITICAL(&stateMux_);
    txBusy_ = false;
    portEXIT_CRITICAL(&stateMux_);
    return false;
  }

  txTransfer_->device_handle = device_;
  txTransfer_->bEndpointAddress = midiEndpointOut_;
  txTransfer_->num_bytes = static_cast<int>(outputLength);
  txTransfer_->flags = 0;
  txTransfer_->callback = transmitCallback;
  txTransfer_->context = this;
  const esp_err_t error = usb_host_transfer_submit(txTransfer_);
  if (error != ESP_OK) {
    portENTER_CRITICAL(&stateMux_);
    txBusy_ = false;
    portEXIT_CRITICAL(&stateMux_);
    Serial.printf("USB-MIDI transmit submit failed: %s\n",
                  esp_err_to_name(error));
    return false;
  }
  return true;
}

void UsbMidiTransport::onTransmitComplete(usb_transfer_t *transfer) {
  const bool success = transfer->status == USB_TRANSFER_STATUS_COMPLETED;
  portENTER_CRITICAL(&stateMux_);
  txBusy_ = false;
  portEXIT_CRITICAL(&stateMux_);
  if (!success && transfer->status != USB_TRANSFER_STATUS_NO_DEVICE) {
    Serial.printf("USB-MIDI transmit failed, status %u\n",
                  static_cast<unsigned>(transfer->status));
  }
}

void UsbMidiTransport::onReceiveComplete(usb_transfer_t *transfer) {
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
    for (int offset = 0; offset + 3 < transfer->actual_num_bytes; offset += 4) {
      const uint8_t *packet = transfer->data_buffer + offset;
      const uint8_t payloadLength = usbMidiPayloadLength(packet[0]);
      consumeMidiBytes(packet + 1, payloadLength);
    }
  }
  if (connected() && transfer->status != USB_TRANSFER_STATUS_NO_DEVICE) {
    transfer->num_bytes = 64;
    const esp_err_t error = usb_host_transfer_submit(transfer);
    if (error != ESP_OK) {
      Serial.printf("USB-MIDI receive resubmit failed: %s\n",
                    esp_err_to_name(error));
    }
  }
}

void UsbMidiTransport::consumeMidiBytes(const uint8_t *midi, size_t length) {
  for (size_t offset = 0; offset < length; ++offset) {
    const uint8_t byte = midi[offset];
    if (byte >= 0xf8) {
      continue;
    }
    if (byte == 0xf0) {
      receivingSysEx_ = true;
      receiveLength_ = 0;
      receiveBuffer_[receiveLength_++] = byte;
      continue;
    }
    if (!receivingSysEx_) {
      continue;
    }
    if (receiveLength_ == kReceiveBufferCapacity) {
      Serial.println("USB-MIDI SysEx response exceeded receive buffer");
      receivingSysEx_ = false;
      receiveLength_ = 0;
      continue;
    }
    receiveBuffer_[receiveLength_++] = byte;
    if (byte == 0xf7) {
      deliverReceived(receiveBuffer_, receiveLength_);
      receivingSysEx_ = false;
      receiveLength_ = 0;
    }
  }
}
