#pragma once

#include "contracts.h"

#include <cstdio>

namespace stomp {

// A slot label is local device numbering, never a confirmed preset name.
inline bool formatPresetSlot(const DeviceCapabilities &capabilities,
                             uint16_t index, char *output, size_t capacity) {
  if (output == nullptr || capacity == 0) return false;
  output[0] = '\0';
  if (index >= capabilities.presetCount) return false;
  unsigned number = index;
  char prefix = 'P';
  switch (capabilities.presetSlotLayout) {
  case PresetSlotLayout::ZeroBasedP: break;
  case PresetSlotLayout::PocketPresetAndEffect:
    if (index < 50) number = index + 1;
    else {
      prefix = 'F';
      number = index - 49;
    }
    break;
  case PresetSlotLayout::Unspecified: return false;
  }
  const int written = snprintf(output, capacity, "%c%02u", prefix, number);
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    output[0] = '\0';
    return false;
  }
  return true;
}

} // namespace stomp
