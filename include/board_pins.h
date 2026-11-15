#pragma once

#include <Arduino.h>

namespace BoardPins {

// Ten two-terminal, normally-open momentary switches. Each switch connects
// its GPIO to GND when pressed; firmware enables the internal pull-up.
inline constexpr uint8_t kSwitches[] = {
    4,  // Switch 1: bank down
    5,  // Switch 2: bank up
    6,  // Switch 3: drive
    7,  // Switch 4: delay
    8,  // Switch 5: reverb
    9,  // Switch 6: preset A
    10, // Switch 7: preset B
    11, // Switch 8: preset C
    12, // Switch 9: preset D
    13, // Switch 10: utility
};

// Labels share the same index as kSwitches.
inline constexpr const char *kSwitchNames[] = {
    "BANK-", "BANK+", "DRIVE", "DELAY", "REVERB",
    "A",     "B",     "C",     "D",     "UTILITY",
};

inline constexpr size_t kSwitchCount =
    sizeof(kSwitches) / sizeof(kSwitches[0]);

static_assert(kSwitchCount == sizeof(kSwitchNames) / sizeof(kSwitchNames[0]),
              "Every switch pin must have a label");

inline constexpr uint8_t kExpressionAdc = 1;
inline constexpr uint8_t kBatteryAdc = 2;
inline constexpr uint8_t kOledSda = 17;
inline constexpr uint8_t kOledScl = 18;
inline constexpr uint8_t kUsbDm = 19;
inline constexpr uint8_t kUsbDp = 20;

} // namespace BoardPins
