#include <cassert>
#include <cstdint>
#include <cstring>

#include "gp5_protocol.h"

static void expectBytes(size_t (*build)(uint8_t *, size_t),
                        const uint8_t *expected, size_t length) {
  uint8_t output[32] = {};
  assert(build(output, sizeof(output)) == length);
  assert(std::memcmp(output, expected, length) == 0);
  assert(build(output, length - 1) == 0);
}

int main() {
  const uint8_t sync[] = {0xf0, 0x00, 0x0e, 0x00, 0x01, 0x00, 0x00,
                          0x00, 0x02, 0x01, 0x02, 0x04, 0x00, 0xf7};
  const uint8_t list[] = {0xf0, 0x03, 0x05, 0x00, 0x01, 0x00, 0x00,
                          0x00, 0x02, 0x01, 0x02, 0x02, 0x04, 0xf7};
  const uint8_t preset[] = {0xf0, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00,
                            0x00, 0x02, 0x01, 0x02, 0x04, 0x03, 0xf7};
  const uint8_t state[] = {0xf0, 0x00, 0x09, 0x00, 0x01, 0x00, 0x00,
                           0x00, 0x02, 0x01, 0x02, 0x04, 0x01, 0xf7};
  expectBytes(buildGp5SyncStartRequest, sync, sizeof(sync));
  expectBytes(buildGp5PatchListRequest, list, sizeof(list));
  expectBytes(buildGp5CurrentPresetRequest, preset, sizeof(preset));
  expectBytes(buildGp5PatchStateRequest, state, sizeof(state));

  uint8_t output[32] = {};
  assert(buildGp5PresetChange(100, output, sizeof(output)) == 0);
  assert(buildGp5PresetChange(0, output, sizeof(output)) == 22);
  assert(output[0] == 0xf0 && output[21] == 0xf7);
  assert(buildGp5BlockToggle(10, true, output, sizeof(output)) == 0);
  assert(buildGp5BlockToggle(2, true, output, sizeof(output)) == 30);

  uint8_t current[] = {0x12, 0x43, 6, 0};
  uint8_t index = 0;
  assert(decodeGp5CurrentPreset(current, sizeof(current), index) && index == 6);
  current[2] = 100;
  assert(!decodeGp5CurrentPreset(current, sizeof(current), index));
  uint8_t malformed[] = {0xf0, 0, 0, 0x10, 0, 0, 0, 0, 0, 0xf7};
  uint8_t command = 0, fragment = 0, payload[128] = {};
  size_t payloadLength = 0;
  assert(!decodeGp5Fragment(malformed, sizeof(malformed), command, fragment,
                            payload, sizeof(payload), payloadLength));
}
