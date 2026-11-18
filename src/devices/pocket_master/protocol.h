#pragma once

#include <cstddef>
#include <cstdint>

namespace stomp::pocket {

constexpr size_t kFramePayloadMax = 100;
constexpr size_t kLiveStateBytes = 476;
constexpr size_t kNameDirectoryBytes = 2002;

struct Frame {
  uint8_t command = 0;
  uint8_t index = 0;
  uint8_t length = 0;
  uint8_t payload[kFramePayloadMax] = {};
};

struct LiveBlocks {
  bool drive = false;
  bool delay = false;
  bool reverb = false;
  bool modelIdsValid = false;
  uint32_t driveModelId = 0;
  uint32_t delayModelId = 0;
  uint32_t reverbModelId = 0;
};

size_t currentPresetRequest(uint8_t *output, size_t capacity);
size_t liveStateRequest(uint8_t *output, size_t capacity);
size_t nameDirectoryRequest(uint8_t *output, size_t capacity);
bool decodeFrame(const uint8_t *midi, size_t length, Frame &frame);
bool decodeCurrentPreset(const uint8_t *payload, size_t length,
                         uint8_t &presetIndex);
bool decodeModuleFeedback(const uint8_t *payload, size_t length,
                          uint8_t &moduleId, bool &enabled);
bool decodeLiveBlocks(const uint8_t *logical, size_t length,
                      LiveBlocks &blocks);

class LiveCollector {
public:
  void reset();
  // Returns true only after all 26 ordered response fragments and the
  // expected 12 41 prefix are present. Invalid ordering resets the collector.
  bool accept(const Frame &frame);
  const uint8_t *logical() const { return logical_; }
  size_t length() const { return length_; }

private:
  uint8_t logical_[kLiveStateBytes] = {};
  uint16_t length_ = 0;
  uint8_t next_ = 0;
};

class NameDirectoryCollector {
public:
  void reset();
  bool accept(const Frame &frame);
  const char *name(uint8_t index) const;
  bool complete() const { return complete_; }

private:
  uint8_t logical_[kNameDirectoryBytes] = {};
  uint8_t next_ = 0;
  bool complete_ = false;
};

} // namespace stomp::pocket
