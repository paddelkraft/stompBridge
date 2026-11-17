#pragma once

#include "contracts.h"

namespace stomp {

class UsbMidiDecoder {
public:
  using Sink = bool (*)(const MidiMessage &, void *);
  UsbMidiDecoder(SessionGeneration generation, Sink sink, void *context)
      : generation_(generation), sink_(sink), context_(context) {}

  void reset(SessionGeneration generation);
  void receiveLost();
  void feed(const uint8_t *bytes, size_t length);
  uint32_t malformedCount() const { return malformed_; }
  uint32_t droppedCount() const { return dropped_; }

private:
  void packet(const uint8_t *bytes);
  void emit(const uint8_t *bytes, size_t length);
  void sysexByte(uint8_t value, bool ending);
  void discardPartial();

  SessionGeneration generation_;
  Sink sink_;
  void *context_;
  uint8_t packetBytes_[4] = {};
  uint8_t packetLength_ = 0;
  uint8_t sysex_[kMaxMidiMessage] = {};
  uint16_t sysexLength_ = 0;
  bool inSysex_ = false;
  uint32_t malformed_ = 0;
  uint32_t dropped_ = 0;
};

} // namespace stomp
