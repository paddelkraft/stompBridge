#include "usb_midi_transport.h"

#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "soc/usb_struct.h"
#include "usb/usb_helpers.h"

namespace {
extern "C" void USBHostBoardInit(void);

#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
bool allowUsbDevice(const usb_device_desc_t *, uint8_t *configuration) {
  if (configuration != nullptr) {
    *configuration = 1;
  }
  return true;
}
#endif

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

  // The USB host emits its own phase-specific logs for root attachment,
  // hub ports, descriptor enumeration, and reset recovery. Keep those visible
  // during pedal bring-up so a failed stage is not mistaken for MIDI failure.
  esp_log_level_set("USBH", ESP_LOG_DEBUG);
  esp_log_level_set("ENUM", ESP_LOG_DEBUG);
  esp_log_level_set("EXT_PORT", ESP_LOG_DEBUG);
  esp_log_level_set("EXT_HUB", ESP_LOG_DEBUG);
  esp_log_level_set("HUB", ESP_LOG_DEBUG);
  Serial.printf(
      "USB host config: hubs=%u multilevel=%u ctrl=%u root-debounce=%u ms "
      "root-reset=%u/%u ms address-recovery=%u ms downstream-attempts=%u "
      "hub-power=%u ms downstream-recovery=%u ms\n",
      static_cast<unsigned>(CONFIG_USB_HOST_HUBS_SUPPORTED),
      static_cast<unsigned>(CONFIG_USB_HOST_HUB_MULTI_LEVEL),
      static_cast<unsigned>(CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE),
      static_cast<unsigned>(CONFIG_USB_HOST_DEBOUNCE_DELAY_MS),
      static_cast<unsigned>(CONFIG_USB_HOST_RESET_HOLD_MS),
      static_cast<unsigned>(CONFIG_USB_HOST_RESET_RECOVERY_MS),
      static_cast<unsigned>(CONFIG_USB_HOST_SET_ADDR_RECOVERY_MS),
      static_cast<unsigned>(CONFIG_USB_HOST_EXT_PORT_RESET_ATTEMPTS),
      static_cast<unsigned>(CONFIG_USB_HOST_EXT_PORT_CUSTOM_POWER_ON_DELAY_MS),
      static_cast<unsigned>(CONFIG_USB_HOST_EXT_PORT_RESET_RECOVERY_DELAY_MS));

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
    const esp_err_t error = usb_host_client_handle_events(
        transport->client_, pdMS_TO_TICKS(100));
    if (error != ESP_OK && error != ESP_ERR_TIMEOUT) {
      Serial.printf("USB host client event handling failed: %s\n",
                    esp_err_to_name(error));
    }
    transport->processOpenRetry();
  }
}

void UsbMidiTransport::scheduleOpenRetry(uint8_t address) {
  constexpr uint8_t kMaxRetries = 5;
  if (retryAttempts_ >= kMaxRetries) {
    retryPending_ = false;
    Serial.printf("USB MIDI setup retries exhausted: address=%u\n",
                  static_cast<unsigned>(address));
    return;
  }

  const uint32_t delayMs = 250u << retryAttempts_;
  ++retryAttempts_;
  retryAddress_ = address;
  retryAtMs_ = millis() + delayMs;
  retryPending_ = true;
  Serial.printf("USB MIDI setup retry scheduled: address=%u attempt=%u/%u "
                "in %lu ms\n",
                static_cast<unsigned>(address),
                static_cast<unsigned>(retryAttempts_),
                static_cast<unsigned>(kMaxRetries),
                static_cast<unsigned long>(delayMs));
}

void UsbMidiTransport::processOpenRetry() {
  if (!retryPending_ ||
      static_cast<int32_t>(millis() - retryAtMs_) < 0) {
    return;
  }

  const uint8_t address = retryAddress_;
  retryPending_ = false;
  Serial.printf("USB MIDI setup retry: address=%u\n",
                static_cast<unsigned>(address));
  if (openMidiDevice(address)) {
    retryAttempts_ = 0;
  } else if (lastOpenFailureRetryable_) {
    scheduleOpenRetry(address);
  }
}

void UsbMidiTransport::poll() {
  const uint32_t port = USB0.hprt;
  constexpr uint32_t kStablePortBits = (1u << 0) | (1u << 2) | (1u << 12) |
                                       (3u << 17);
  const uint32_t portSummary = port & kStablePortBits;
  if (portSummary != lastPortStatus_) {
    lastPortStatus_ = portSummary;
    Serial.printf(
        "USB root port: connected=%u enabled=%u power=%u speed=%u\n",
        static_cast<unsigned>(port & 1u),
        static_cast<unsigned>((port >> 2) & 1u),
        static_cast<unsigned>((port >> 12) & 1u),
        static_cast<unsigned>((port >> 17) & 3u));
  }
}

bool UsbMidiTransport::connected() const {
  portENTER_CRITICAL(&stateMux_);
  const bool value = connected_;
  portEXIT_CRITICAL(&stateMux_);
  return value;
}

bool UsbMidiTransport::connectedDevice(MidiDeviceIdentity &identity) const {
  portENTER_CRITICAL(&stateMux_);
  const bool value = connected_ && deviceIdentity_.vendorId != 0;
  if (value) {
    identity = deviceIdentity_;
  }
  portEXIT_CRITICAL(&stateMux_);
  return value;
}

void UsbMidiTransport::onClientEvent(
    const usb_host_client_event_msg_t &event) {
  if (event.event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    Serial.printf("USB device announcement: address=%u; opening enumerated device\n",
                  static_cast<unsigned>(event.new_dev.address));
    retryPending_ = false;
    retryAttempts_ = 0;
    if (!openMidiDevice(event.new_dev.address) &&
        lastOpenFailureRetryable_) {
      scheduleOpenRetry(event.new_dev.address);
    }
    return;
  }
  if (event.event != USB_HOST_CLIENT_EVENT_DEV_GONE || device_ == nullptr ||
      event.dev_gone.dev_hdl != device_) {
    return;
  }

  usb_device_handle_t goneDevice = device_;
  portENTER_CRITICAL(&stateMux_);
  connected_ = false;
  txBusy_ = false;
  deviceIdentity_ = {};
  receivingSysEx_ = false;
  receiveLength_ = 0;
  portEXIT_CRITICAL(&stateMux_);
  device_ = nullptr;
  retryPending_ = false;
  retryAttempts_ = 0;
  usb_host_device_close(client_, goneDevice);
  Serial.println("USB-MIDI disconnected");
}

bool UsbMidiTransport::openMidiDevice(uint8_t address) {
  lastOpenFailureRetryable_ = false;
  usb_device_handle_t candidate = nullptr;
  esp_err_t error = usb_host_device_open(client_, address, &candidate);
  if (error != ESP_OK) {
    lastOpenFailureRetryable_ = true;
    Serial.printf("USB open failed: address=%u error=%s\n",
                  static_cast<unsigned>(address), esp_err_to_name(error));
    return false;
  }

  const usb_device_desc_t *descriptor = nullptr;
  error = usb_host_get_device_descriptor(candidate, &descriptor);
  if (error != ESP_OK || descriptor == nullptr) {
    lastOpenFailureRetryable_ = true;
    Serial.printf("USB full device descriptor failed: address=%u error=%s\n",
                  static_cast<unsigned>(address), esp_err_to_name(error));
    usb_host_device_close(client_, candidate);
    return false;
  }
  Serial.printf("USB full device descriptor: address=%u VID:PID=%04X:%04X "
                "EP0=%u configurations=%u\n",
                static_cast<unsigned>(address), descriptor->idVendor,
                descriptor->idProduct, descriptor->bMaxPacketSize0,
                descriptor->bNumConfigurations);
  if (!discoverMidiInterface(candidate)) {
    Serial.println("USB device has no usable MIDI streaming interface");
    usb_host_device_close(client_, candidate);
    return false;
  }

  error = usb_host_interface_claim(client_, candidate, midiInterface_, 0);
  if (error != ESP_OK) {
    lastOpenFailureRetryable_ = true;
    Serial.printf("USB-MIDI interface claim failed: %s\n",
                  esp_err_to_name(error));
    usb_host_device_close(client_, candidate);
    return false;
  }
  Serial.printf("USB MIDI interface claim succeeded: interface=%u\n",
                static_cast<unsigned>(midiInterface_));

  if (rxTransfer_ == nullptr &&
      usb_host_transfer_alloc(kUsbReceiveTransferSize, 0, &rxTransfer_) !=
          ESP_OK) {
    lastOpenFailureRetryable_ = true;
    Serial.println("USB-MIDI receive allocation failed");
    usb_host_interface_release(client_, candidate, midiInterface_);
    usb_host_device_close(client_, candidate);
    return false;
  }
  if (txTransfer_ == nullptr &&
      usb_host_transfer_alloc(128, 0, &txTransfer_) != ESP_OK) {
    lastOpenFailureRetryable_ = true;
    Serial.println("USB-MIDI transmit allocation failed");
    usb_host_interface_release(client_, candidate, midiInterface_);
    usb_host_device_close(client_, candidate);
    return false;
  }

  device_ = candidate;
  rxTransfer_->device_handle = device_;
  rxTransfer_->bEndpointAddress = midiEndpointIn_;
  rxTransfer_->num_bytes = kUsbReceiveTransferSize;
  rxTransfer_->callback = receiveCallback;
  rxTransfer_->context = this;
  portENTER_CRITICAL(&stateMux_);
  connected_ = true;
  deviceIdentity_.vendorId = descriptor->idVendor;
  deviceIdentity_.productId = descriptor->idProduct;
  portEXIT_CRITICAL(&stateMux_);
  error = usb_host_transfer_submit(rxTransfer_);
  if (error != ESP_OK) {
    lastOpenFailureRetryable_ = true;
    Serial.printf("USB-MIDI receive start failed: %s\n",
                  esp_err_to_name(error));
    portENTER_CRITICAL(&stateMux_);
    connected_ = false;
    deviceIdentity_ = {};
    portEXIT_CRITICAL(&stateMux_);
    device_ = nullptr;
    usb_host_interface_release(client_, candidate, midiInterface_);
    usb_host_device_close(client_, candidate);
    return false;
  }

  Serial.printf("USB-MIDI connected: %04X:%04X\n", descriptor->idVendor,
                descriptor->idProduct);
  return true;
}

bool UsbMidiTransport::discoverMidiInterface(usb_device_handle_t device) {
  const usb_config_desc_t *configuration = nullptr;
  const esp_err_t error =
      usb_host_get_active_config_descriptor(device, &configuration);
  if (error != ESP_OK || configuration == nullptr) {
    lastOpenFailureRetryable_ = true;
    Serial.println("USB configuration descriptor retrieval failed");
    return false;
  }
  Serial.printf("USB configuration descriptor: %u bytes, %u interfaces\n",
                static_cast<unsigned>(configuration->wTotalLength),
                static_cast<unsigned>(configuration->bNumInterfaces));
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
  Serial.println("USB MIDI Streaming interface/endpoints not found in configuration");
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
  if (success && transfer->num_bytes > 0) {
    Serial.printf("USB MIDI traffic: OUT %d bytes\n", transfer->num_bytes);
  }
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
    if (transfer->actual_num_bytes > 0) {
      Serial.printf("USB MIDI traffic: IN %d bytes\n",
                    transfer->actual_num_bytes);
    }
    for (int offset = 0; offset + 3 < transfer->actual_num_bytes; offset += 4) {
      const uint8_t *packet = transfer->data_buffer + offset;
      const uint8_t payloadLength = usbMidiPayloadLength(packet[0]);
      consumeMidiBytes(packet + 1, payloadLength);
    }
  } else if (transfer->status != USB_TRANSFER_STATUS_NO_DEVICE) {
    // Do not retain a partial SysEx after an overflow: the host has discarded
    // an unknown number of USB-MIDI event packets, so subsequent bytes cannot
    // be safely joined to the previous frame.
    receivingSysEx_ = false;
    receiveLength_ = 0;
    Serial.printf("USB-MIDI receive failed, status %u actual=%d requested=%d\n",
                  static_cast<unsigned>(transfer->status),
                  transfer->actual_num_bytes, transfer->num_bytes);
    if (device_ != nullptr) {
      const esp_err_t flushError =
          usb_host_endpoint_flush(device_, midiEndpointIn_);
      const esp_err_t clearError =
          usb_host_endpoint_clear(device_, midiEndpointIn_);
      Serial.printf("USB-MIDI IN endpoint error recovery: flush=%s clear=%s\n",
                    esp_err_to_name(flushError),
                    esp_err_to_name(clearError));
    }
  }
  if (connected() && transfer->status != USB_TRANSFER_STATUS_NO_DEVICE) {
    transfer->num_bytes = kUsbReceiveTransferSize;
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
