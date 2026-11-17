#pragma once

#include <cstdint>

// Returns a display name for the model identifier in a GP-5 patch dump.
// Supported block indices are 2 (DST), 7 (DLY), and 8 (RVB).
const char *gp5EffectModelName(uint8_t blockIndex, uint32_t modelId);
