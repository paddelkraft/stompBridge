#pragma once

#include <Arduino.h>
#include <usb/usb_host.h>

#include "midi_transport.h"

// USB-MIDI host transport backed by ESP-IDF's USB Host Library. Unlike the
// TinyUSB Arduino host wrapper, this stack enumerates the GP-5's internal USB
// hub before opening its downstream MIDI interface.
class UsbMidiTransport : public MidiTransport {
public:
  const char *name() const override { return "USB-MIDI"; }
  bool begin() override;
  void poll() override;
  bool connected() const override;
  bool send(const uint8_t *midi, size_t length) override;

  void onClientEvent(const usb_host_client_event_msg_t &event);
  void onReceiveComplete(usb_transfer_t *transfer);
  void onTransmitComplete(usb_transfer_t *transfer);

private:
  static constexpr uint16_t kGp5Vendor = 0x84ef;
  static constexpr uint16_t kGp5Product = 0x0184;
  static constexpr uint16_t kPocketMasterProduct = 0x0284;
  static constexpr size_t kReceiveBufferCapacity = 256;

  static void daemonTask(void *context);
  static void clientTask(void *context);
  bool openMidiDevice(uint8_t address);
  bool discoverMidiInterface(usb_device_handle_t device);
  void consumeMidiBytes(const uint8_t *midi, size_t length);

  bool started_ = false;
  bool connected_ = false;
  bool txBusy_ = false;
  usb_host_client_handle_t client_ = nullptr;
  usb_device_handle_t device_ = nullptr;
  usb_transfer_t *rxTransfer_ = nullptr;
  usb_transfer_t *txTransfer_ = nullptr;
  uint8_t midiInterface_ = 0xff;
  uint8_t midiEndpointOut_ = 0;
  uint8_t midiEndpointIn_ = 0;
  TaskHandle_t daemonTask_ = nullptr;
  TaskHandle_t clientTask_ = nullptr;
  bool receivingSysEx_ = false;
  size_t receiveLength_ = 0;
  uint8_t receiveBuffer_[kReceiveBufferCapacity] = {};
  uint32_t lastPortStatus_ = 0xffffffffu;
  mutable portMUX_TYPE stateMux_ = portMUX_INITIALIZER_UNLOCKED;
};
