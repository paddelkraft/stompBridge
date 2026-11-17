#pragma once

#include <Arduino.h>

struct MidiDeviceIdentity {
  uint16_t vendorId = 0;
  uint16_t productId = 0;
};

// Device profiles and input handling depend only on this interface. USB-MIDI,
// BLE-MIDI, and future transports own their connection and framing details.
class MidiTransport {
public:
  using ReceiveHandler = void (*)(const uint8_t *midi, size_t length,
                                  void *context);

  virtual ~MidiTransport() = default;

  virtual const char *name() const = 0;
  virtual bool begin() = 0;
  virtual void poll() = 0;
  virtual bool connected() const = 0;
  virtual bool send(const uint8_t *midi, size_t length) = 0;
  // USB transports can report the enumerated device. Other transports may
  // return false when their peer has no stable hardware identity.
  virtual bool connectedDevice(MidiDeviceIdentity &) const { return false; }

  void setReceiveHandler(ReceiveHandler handler, void *context = nullptr) {
    receiveHandler_ = handler;
    receiveContext_ = context;
  }

  bool sendControlChange(uint8_t channel, uint8_t control, uint8_t value) {
    if (channel < 1 || channel > 16) {
      return false;
    }
    const uint8_t message[] = {
        static_cast<uint8_t>(0xb0 | (channel - 1)), control, value};
    return send(message, sizeof(message));
  }

protected:
  void deliverReceived(const uint8_t *midi, size_t length) {
    if (receiveHandler_ != nullptr && midi != nullptr && length != 0) {
      receiveHandler_(midi, length, receiveContext_);
    }
  }

private:
  ReceiveHandler receiveHandler_ = nullptr;
  void *receiveContext_ = nullptr;
};
