#include "usb_midi_decoder.h"

#include <cstring>

namespace stomp {

namespace {
constexpr uint8_t lengths[16] = {0, 0, 2, 3, 3, 1, 2, 3,
                                  3, 3, 3, 3, 3, 3, 3, 1};
}

void UsbMidiDecoder::discardPartial() {
  packetLength_ = 0;
  sysexLength_ = 0;
  inSysex_ = false;
}

void UsbMidiDecoder::reset(SessionGeneration generation) {
  generation_ = generation;
  discardPartial();
}

void UsbMidiDecoder::receiveLost() {
  discardPartial();
  ++dropped_;
}

void UsbMidiDecoder::emit(const uint8_t *bytes, size_t length) {
  if (sink_ == nullptr || length > kMaxMidiMessage) {
    ++dropped_;
    return;
  }
  MidiMessage message;
  message.generation = generation_;
  message.length = static_cast<uint16_t>(length);
  memcpy(message.bytes, bytes, length);
  if (!sink_(message, context_)) ++dropped_;
}

void UsbMidiDecoder::sysexByte(uint8_t value, bool ending) {
  if (value >= 0xf8) {
    emit(&value, 1);
    return;
  }
  if (value == 0xf0) {
    if (inSysex_) ++malformed_;
    inSysex_ = true;
    sysexLength_ = 0;
  } else if (!inSysex_) {
    ++malformed_;
    return;
  }
  if (sysexLength_ == kMaxMidiMessage) {
    ++dropped_;
    discardPartial();
    return;
  }
  sysex_[sysexLength_++] = value;
  if (ending && value == 0xf7) {
    emit(sysex_, sysexLength_);
    inSysex_ = false;
    sysexLength_ = 0;
  }
}

void UsbMidiDecoder::packet(const uint8_t *bytes) {
  const uint8_t cin = bytes[0] & 0x0f;
  const uint8_t length = lengths[cin];
  if (length == 0) {
    ++malformed_;
    discardPartial();
    return;
  }
  const uint8_t *midi = bytes + 1;
  if (cin == 5 && !inSysex_ && midi[0] != 0xf7) {
    if (midi[0] == 0xf6) emit(midi, 1);
    else ++malformed_;
    return;
  }
  if (cin == 4 || cin == 5 || cin == 6 || cin == 7) {
    for (uint8_t i = 0; i < length; ++i)
      sysexByte(midi[i], cin >= 5 && i + 1 == length);
    if (cin >= 5 && midi[length - 1] != 0xf7) {
      ++malformed_;
      discardPartial();
    }
    if (cin == 4 && midi[length - 1] == 0xf7) {
      ++malformed_;
      discardPartial();
    }
    return;
  }
  if (cin == 15 && midi[0] >= 0xf8) {
    emit(midi, 1);
    return;
  }
  if (inSysex_) {
    ++malformed_;
    inSysex_ = false;
    sysexLength_ = 0;
  }
  if ((cin == 2 && midi[0] != 0xf1 && midi[0] != 0xf3) ||
      (cin == 3 && midi[0] != 0xf2)) {
    ++malformed_;
    return;
  }
  if ((cin >= 8 && cin <= 14 && (midi[0] >> 4) != cin) ||
      (cin == 15 && midi[0] < 0xf8)) {
    ++malformed_;
    return;
  }
  emit(midi, length);
}

void UsbMidiDecoder::feed(const uint8_t *bytes, size_t length) {
  if (bytes == nullptr && length != 0) {
    ++malformed_;
    discardPartial();
    return;
  }
  for (size_t i = 0; i < length; ++i) {
    packetBytes_[packetLength_++] = bytes[i];
    if (packetLength_ == 4) {
      packetLength_ = 0;
      packet(packetBytes_);
    }
  }
}

} // namespace stomp
