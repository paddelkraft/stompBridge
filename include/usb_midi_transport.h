#pragma once

#include <Arduino.h>
#include <usb/usb_host.h>

#include "midi_transport.h"

// USB-MIDI host transport backed by ESP-IDF's USB Host Library. Unlike the
// TinyUSB Arduino host wrapper, this stack enumerates the GP-5's internal USB
// hub before opening its downstream MIDI interface.
class UsbMidiTransport : public MidiTransport {
public:
  enum class Event : uint8_t { TransmitComplete, ReceiveLost };
  using EventHandler = void (*)(Event event, bool success, void *context);

  const char *name() const override { return "USB-MIDI"; }
  bool begin() override;
  void poll() override;
  bool connected() const override;
  bool connectedDevice(MidiDeviceIdentity &identity) const override;
  bool send(const uint8_t *midi, size_t length) override;
  // Configure before begin(). Errors remain visible when verbose traffic is off.
  void setVerboseTraffic(bool enabled) { verboseTraffic_ = enabled; }

  void onClientEvent(const usb_host_client_event_msg_t &event);
  void onReceiveComplete(usb_transfer_t *transfer);
  void onTransmitComplete(usb_transfer_t *transfer);
  // Configure before begin(); callbacks run on the USB client task.
  void setEventHandler(EventHandler handler, void *context = nullptr) {
    eventHandler_ = handler;
    eventContext_ = context;
  }

private:
  // A GP-5 patch dump spans several nibble-expanded SysEx fragments. Keep
  // both the endpoint request and the assembled message comfortably above
  // one fragment so a busy device cannot overrun the host receive request.
  static constexpr size_t kReceiveBufferCapacity = 2048;
  static constexpr size_t kUsbReceiveTransferSize = 4096;

  static void daemonTask(void *context);
  static void clientTask(void *context);
  bool openMidiDevice(uint8_t address);
  void scheduleOpenRetry(uint8_t address);
  void processOpenRetry();
  void processCloseRetry();
  void processReceiveRetry();
  bool discoverMidiInterface(usb_device_handle_t device);
  void consumeMidiBytes(const uint8_t *midi, size_t length);

  bool started_ = false;
  bool verboseTraffic_ = true;
  bool connected_ = false;
  bool txBusy_ = false;
  bool lastOpenFailureRetryable_ = false;
  bool retryPending_ = false;
  uint8_t retryAddress_ = 0;
  uint8_t retryAttempts_ = 0;
  uint32_t retryAtMs_ = 0;
  MidiDeviceIdentity deviceIdentity_;
  usb_host_client_handle_t client_ = nullptr;
  usb_device_handle_t device_ = nullptr;
  usb_device_handle_t closingDevice_ = nullptr;
  bool closingInterfaceReleased_ = false;
  uint32_t closeRetryAtMs_ = 0;
  usb_transfer_t *rxTransfer_[2] = {};
  bool rxSubmitted_[2] = {};
  bool rxRecoveryPending_ = false;
  uint8_t rxRecoveryAttempts_ = 0;
  uint32_t rxRecoveryAtMs_ = 0;
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
  EventHandler eventHandler_ = nullptr;
  void *eventContext_ = nullptr;
};
