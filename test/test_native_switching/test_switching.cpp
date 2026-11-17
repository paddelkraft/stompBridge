#include <unity.h>

#include "core/switching.h"

using namespace stomp;

void test_immediate_after_debounce_and_bounce_filter() {
  GestureConfig config;
  Switching input(config);
  GestureEvent event;
  input.sample(0, false, 0);
  input.sample(0, true, 10);
  input.sample(0, false, 20);
  input.sample(0, true, 30);
  input.sample(0, true, 54);
  TEST_ASSERT_FALSE(input.pop(event));
  input.sample(0, true, 55);
  TEST_ASSERT_TRUE(input.pop(event));
  TEST_ASSERT_EQUAL(GestureKind::Press, event.kind);
  TEST_ASSERT_EQUAL_UINT32(55, event.atMs);
  input.sample(0, true, 1000);
  TEST_ASSERT_FALSE(input.pop(event));
}

void test_deferred_tap_and_hold_are_exclusive() {
  GestureConfig config;
  config.policy[1] = BindingPolicy::Deferred;
  Switching input(config);
  GestureEvent event;
  input.sample(1, false, 0);
  input.sample(1, true, 1);
  input.sample(1, true, 26);
  input.tick(85);
  TEST_ASSERT_FALSE(input.pop(event));
  input.tick(86);
  input.sample(1, false, 90);
  input.sample(1, false, 115);
  input.tick(115);
  TEST_ASSERT_TRUE(input.pop(event));
  TEST_ASSERT_EQUAL(GestureKind::Tap, event.kind);
  TEST_ASSERT_FALSE(input.pop(event));
  input.sample(1, true, 200);
  input.sample(1, true, 225);
  input.tick(724);
  TEST_ASSERT_FALSE(input.pop(event));
  input.tick(725);
  TEST_ASSERT_TRUE(input.pop(event));
  TEST_ASSERT_EQUAL(GestureKind::Hold, event.kind);
  input.sample(1, false, 800);
  input.sample(1, false, 825);
  input.tick(825);
  TEST_ASSERT_FALSE(input.pop(event));
}

void test_largest_chord_consumes_singles() {
  GestureConfig config;
  config.policy[0] = BindingPolicy::Deferred;
  config.policy[1] = BindingPolicy::Deferred;
  config.policy[2] = BindingPolicy::Deferred;
  config.chordCount = 2;
  config.chords[0].switches = 0b011;
  config.chords[1].switches = 0b111;
  Switching input(config);
  TEST_ASSERT_TRUE(input.valid());
  GestureEvent event;
  for (uint8_t i = 0; i < 3; ++i) input.sample(i, false, 0);
  for (uint8_t i = 0; i < 3; ++i) input.sample(i, true, 10 + i);
  for (uint8_t i = 0; i < 3; ++i) input.sample(i, true, 35 + i);
  input.tick(95);
  TEST_ASSERT_TRUE(input.pop(event));
  TEST_ASSERT_EQUAL(GestureKind::Chord, event.kind);
  TEST_ASSERT_EQUAL_HEX16(0b111, event.switches);
  for (uint8_t i = 0; i < 3; ++i) {
    input.sample(i, false, 100);
    input.sample(i, false, 125);
  }
  input.tick(600);
  TEST_ASSERT_FALSE(input.pop(event));
}

void test_startup_held_and_timer_wrap() {
  GestureConfig config;
  Switching input(config);
  GestureEvent event;
  input.sample(0, true, 0xfffffff0u);
  input.sample(0, true, 30);
  TEST_ASSERT_FALSE(input.pop(event));
  input.sample(0, false, 31);
  input.sample(0, false, 56);
  input.sample(0, true, 57);
  input.sample(0, true, 82);
  TEST_ASSERT_TRUE(input.pop(event));
  TEST_ASSERT_EQUAL(GestureKind::Press, event.kind);
}

void test_incomplete_chord_falls_back_to_hold() {
  GestureConfig config;
  config.policy[0] = BindingPolicy::Deferred;
  config.policy[1] = BindingPolicy::Deferred;
  config.chordCount = 1;
  config.chords[0].switches = 0b11;
  Switching input(config);
  GestureEvent event;
  input.sample(0, false, 0);
  input.sample(1, false, 0);
  input.sample(0, true, 1);
  input.sample(0, true, 26);
  input.tick(86);
  TEST_ASSERT_FALSE(input.pop(event));
  input.tick(526);
  TEST_ASSERT_TRUE(input.pop(event));
  TEST_ASSERT_EQUAL(GestureKind::Hold, event.kind);
  TEST_ASSERT_EQUAL_HEX16(1, event.switches);
}

void test_equal_size_chord_priority_and_release_rearm() {
  GestureConfig config;
  for (auto &policy : config.policy) policy = BindingPolicy::Deferred;
  config.chordCount = 2;
  config.chords[0] = {0b011, 2};
  config.chords[1] = {0b110, 1};
  Switching input(config);
  GestureEvent event;
  for (uint8_t i = 0; i < 3; ++i) input.sample(i, false, 0);
  for (uint8_t i = 0; i < 3; ++i) input.sample(i, true, 1);
  for (uint8_t i = 0; i < 3; ++i) input.sample(i, true, 26);
  input.tick(86);
  TEST_ASSERT_TRUE(input.pop(event));
  TEST_ASSERT_EQUAL_HEX16(0b110, event.switches);
  input.sample(1, false, 90);
  input.sample(1, false, 115);
  input.sample(1, true, 120);
  input.sample(1, true, 145);
  input.tick(645);
  TEST_ASSERT_TRUE(input.pop(event));
  TEST_ASSERT_EQUAL(GestureKind::Hold, event.kind);
  TEST_ASSERT_EQUAL_HEX16(0b001, event.switches);
  TEST_ASSERT_FALSE(input.pop(event));
  input.sample(1, false, 650);
  input.sample(2, false, 650);
  input.sample(1, false, 675);
  input.sample(2, false, 675);
  input.sample(0, false, 650);
  input.sample(0, false, 675);
  input.sample(1, true, 680);
  input.sample(2, true, 680);
  input.sample(1, true, 705);
  input.sample(2, true, 705);
  input.tick(765);
  TEST_ASSERT_TRUE(input.pop(event));
  TEST_ASSERT_EQUAL(GestureKind::Chord, event.kind);
  TEST_ASSERT_EQUAL_HEX16(0b110, event.switches);
}

void test_invalid_chord_binding_is_rejected() {
  GestureConfig config;
  config.chordCount = 1;
  config.chords[0].switches = 0b11;
  Switching input(config);
  TEST_ASSERT_FALSE(input.valid());
  config.policy[0] = BindingPolicy::Deferred;
  config.policy[1] = BindingPolicy::Deferred;
  config.policy[2] = BindingPolicy::Deferred;
  config.chordCount = 2;
  config.chords[0] = {0b011, 0};
  config.chords[1] = {0b110, 0};
  Switching ambiguous(config);
  TEST_ASSERT_FALSE(ambiguous.valid());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_immediate_after_debounce_and_bounce_filter);
  RUN_TEST(test_deferred_tap_and_hold_are_exclusive);
  RUN_TEST(test_largest_chord_consumes_singles);
  RUN_TEST(test_startup_held_and_timer_wrap);
  RUN_TEST(test_incomplete_chord_falls_back_to_hold);
  RUN_TEST(test_equal_size_chord_priority_and_release_rearm);
  RUN_TEST(test_invalid_chord_binding_is_rejected);
  return UNITY_END();
}
