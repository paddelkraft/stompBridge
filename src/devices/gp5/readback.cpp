#include "readback.h"
#include "gp5_protocol.h"

namespace stomp::gp5 {

bool currentPreset(const uint8_t *midi, size_t length, uint8_t &index) {
  if (midi == nullptr || length != 18) return false;
  uint8_t command = 0;
  uint8_t fragment = 0;
  uint8_t payload[4] = {};
  size_t payloadLength = 0;
  if (!decodeGp5Fragment(midi, length, command, fragment, payload,
                         sizeof(payload), payloadLength) ||
      command != 1 || fragment != 0 || payloadLength != 4 ||
      payload[0] != 0x12 || payload[1] != 0x43 || payload[2] > 99 ||
      payload[3] != 0) return false;
  index = payload[2];
  return true;
}

bool patchStateFragment(const uint8_t *midi, size_t length, Blocks &blocks) {
  if (midi == nullptr || length != 48) return false;
  uint8_t command = 0;
  uint8_t fragment = 0;
  uint8_t payload[19] = {};
  size_t payloadLength = 0;
  if (!decodeGp5Fragment(midi, length, command, fragment, payload,
                         sizeof(payload), payloadLength) ||
      command != 0x19 || fragment != 3 || payloadLength != 19)
    return false;
  // The host decoder reads body bytes 34..37. Including F0 shifts by one.
  const uint32_t mask = uint32_t(midi[35]) | (uint32_t(midi[36]) << 8) |
                        (uint32_t(midi[37]) << 16) |
                        (uint32_t(midi[38]) << 24);
  blocks.drive = (mask & (1u << 10)) != 0;
  blocks.delay = (mask & (1u << 3)) != 0;
  blocks.reverb = (mask & (1u << 24)) != 0;
  return true;
}

} // namespace stomp::gp5
