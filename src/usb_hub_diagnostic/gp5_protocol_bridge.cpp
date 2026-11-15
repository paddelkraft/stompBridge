#include "gp5_protocol.h"

extern "C" size_t gp5_build_preset_change(uint8_t preset_index,
                                            uint8_t *output,
                                            size_t capacity) {
  return buildGp5PresetChange(preset_index, output, capacity);
}

extern "C" size_t gp5_build_block_toggle(uint8_t block_index, bool enabled,
                                          uint8_t *output,
                                          size_t capacity) {
  return buildGp5BlockToggle(block_index, enabled, output, capacity);
}
