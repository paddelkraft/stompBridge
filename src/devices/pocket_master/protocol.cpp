#include "protocol.h"

#include <cstring>

namespace stomp::pocket {

namespace {

constexpr uint8_t kPresetRequest[] = {0xf0, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00,
                                      0x00, 0x02, 0x01, 0x02, 0x04, 0x03, 0xf7};
constexpr uint8_t kLiveRequest[] = {0xf0, 0x00, 0x09, 0x00, 0x01, 0x00, 0x00,
                                    0x00, 0x02, 0x01, 0x02, 0x04, 0x01, 0xf7};

uint8_t crc8(const uint8_t *bytes, size_t length) {
  uint8_t crc = 0;
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x07) : uint8_t(crc << 1);
  }
  return crc;
}

uint16_t u16(const uint8_t *value) {
  return uint16_t(value[0]) | (uint16_t(value[1]) << 8);
}

bool findTlv(const uint8_t *bytes, size_t length, uint16_t wanted,
             const uint8_t *&value, size_t &valueLength) {
  size_t offset = 0;
  bool found = false;
  while (offset < length) {
    if (length - offset < 4) return false;
    const uint16_t id = u16(bytes + offset);
    const size_t size = u16(bytes + offset + 2);
    offset += 4;
    if (size > length - offset) return false;
    if (id == wanted) {
      if (found) return false;
      value = bytes + offset;
      valueLength = size;
      found = true;
    }
    offset += size;
  }
  return found;
}

} // namespace

size_t currentPresetRequest(uint8_t *output, size_t capacity) {
  if (output == nullptr || capacity < sizeof(kPresetRequest)) return 0;
  memcpy(output, kPresetRequest, sizeof(kPresetRequest));
  return sizeof(kPresetRequest);
}

size_t liveStateRequest(uint8_t *output, size_t capacity) {
  if (output == nullptr || capacity < sizeof(kLiveRequest)) return 0;
  memcpy(output, kLiveRequest, sizeof(kLiveRequest));
  return sizeof(kLiveRequest);
}

bool decodeFrame(const uint8_t *midi, size_t length, Frame &frame) {
  if (midi == nullptr || length < 10 || midi[0] != 0xf0 ||
      midi[length - 1] != 0xf7 || (length & 1u)) return false;
  const size_t bodyLength = (length - 2) / 2;
  if (bodyLength < 4 || bodyLength - 4 > kFramePayloadMax) return false;
  uint8_t body[4 + kFramePayloadMax] = {};
  for (size_t i = 0; i < bodyLength; ++i) {
    const uint8_t high = midi[1 + i * 2];
    const uint8_t low = midi[2 + i * 2];
    if (high > 0x0f || low > 0x0f) return false;
    body[i] = uint8_t((high << 4) | low);
  }
  if (body[3] != bodyLength - 4 || body[0] != crc8(body + 1, bodyLength - 1))
    return false;
  frame.command = body[1];
  frame.index = body[2];
  frame.length = body[3];
  memcpy(frame.payload, body + 4, frame.length);
  return true;
}

bool decodeCurrentPreset(const uint8_t *payload, size_t length,
                         uint8_t &presetIndex) {
  if (payload == nullptr || length != 4 || payload[0] != 0x12 ||
      payload[1] != 0x43 || payload[3] != 0 || payload[2] > 99) return false;
  presetIndex = payload[2];
  return true;
}

bool decodeModuleFeedback(const uint8_t *payload, size_t length,
                          uint8_t &moduleId, bool &enabled) {
  if (payload == nullptr || length != 10 || payload[0] != 0x12 ||
      payload[1] != 0x49 || payload[2] >= 9) return false;
  moduleId = payload[2];
  enabled = payload[6] != 0;
  return true;
}

bool decodeLiveBlocks(const uint8_t *logical, size_t length,
                      LiveBlocks &blocks) {
  if (logical == nullptr || length != kLiveStateBytes || logical[0] != 0x12 ||
      logical[1] != 0x41) return false;
  const uint8_t *section = nullptr;
  size_t sectionLength = 0;
  if (!findTlv(logical + 2, length - 2, 0x0002, section, sectionLength))
    return false;
  const uint8_t *bitmap = nullptr;
  size_t bitmapLength = 0;
  const uint8_t *chain = nullptr;
  size_t chainLength = 0;
  if (!findTlv(section, sectionLength, 0x3001, bitmap, bitmapLength) ||
      bitmapLength != 4 ||
      !findTlv(section, sectionLength, 0x3002, chain, chainLength) ||
      chainLength != 10) return false;
  uint16_t seen = 0;
  for (size_t i = 0; i < chainLength; ++i) {
    if (chain[i] >= 10 || (seen & (1u << chain[i]))) return false;
    seen |= 1u << chain[i];
  }
  const uint32_t mask = uint32_t(bitmap[0]) | (uint32_t(bitmap[1]) << 8) |
                        (uint32_t(bitmap[2]) << 16) | (uint32_t(bitmap[3]) << 24);
  blocks.drive = (mask & (1u << 2)) != 0;
  blocks.delay = (mask & (1u << 7)) != 0;
  blocks.reverb = (mask & (1u << 8)) != 0;
  return true;
}

void LiveCollector::reset() {
  length_ = 0;
  next_ = 0;
}

bool LiveCollector::accept(const Frame &frame) {
  if (frame.command != 0x1a) return false;
  if (next_ == 0 && frame.index != 0) return false;
  if (frame.index != next_ || frame.index >= 26 ||
      frame.length > kLiveStateBytes - length_) {
    reset();
    return false;
  }
  memcpy(logical_ + length_, frame.payload, frame.length);
  length_ += frame.length;
  ++next_;
  if (next_ != 26) return false;
  const bool complete = length_ == kLiveStateBytes && logical_[0] == 0x12 &&
                        logical_[1] == 0x41;
  if (!complete) reset();
  return complete;
}

} // namespace stomp::pocket
