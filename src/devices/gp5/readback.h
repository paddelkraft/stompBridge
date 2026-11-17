#pragma once

#include <cstddef>
#include <cstdint>

namespace stomp::gp5 {

struct Blocks {
  bool drive = false;
  bool delay = false;
  bool reverb = false;
};

// Exact USB response shapes retained from the hardware-tested Python host.
// Callers pass complete F0...F7 MIDI messages, not BLE-MIDI packets.
bool currentPreset(const uint8_t *midi, size_t length, uint8_t &index);
bool patchStateFragment(const uint8_t *midi, size_t length, Blocks &blocks);

} // namespace stomp::gp5
