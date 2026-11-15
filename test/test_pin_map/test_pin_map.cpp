#include <unity.h>

#include "board_pins.h"

void test_switch_count_is_ten() {
  TEST_ASSERT_EQUAL_UINT32(10, BoardPins::kSwitchCount);
}

void test_switch_labels_match_pin_count() {
  TEST_ASSERT_EQUAL_UINT32(
      BoardPins::kSwitchCount,
      sizeof(BoardPins::kSwitchNames) / sizeof(BoardPins::kSwitchNames[0]));
}

void test_switch_pins_are_unique() {
  for (size_t i = 0; i < BoardPins::kSwitchCount; ++i) {
    for (size_t j = i + 1; j < BoardPins::kSwitchCount; ++j) {
      TEST_ASSERT_NOT_EQUAL(BoardPins::kSwitches[i], BoardPins::kSwitches[j]);
    }
  }
}

void test_switches_do_not_use_reserved_pins() {
  constexpr uint8_t reserved[] = {0, 19, 20, 35, 36, 37, 45, 46};

  for (const uint8_t switchPin : BoardPins::kSwitches) {
    for (const uint8_t reservedPin : reserved) {
      TEST_ASSERT_NOT_EQUAL(reservedPin, switchPin);
    }
  }
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_switch_count_is_ten);
  RUN_TEST(test_switch_labels_match_pin_count);
  RUN_TEST(test_switch_pins_are_unique);
  RUN_TEST(test_switches_do_not_use_reserved_pins);
  UNITY_END();
}

void loop() {}
