#include <unity.h>

#include "gp5_protocol.h"
#include "gp5_models.h"

#include <cstring>

void test_read_only_request_and_bounds() {
  uint8_t message[14] = {};
  TEST_ASSERT_EQUAL_UINT32(14, buildGp5PatchStateRequest(message, sizeof(message)));
  TEST_ASSERT_EQUAL_HEX8(0xf0, message[0]);
  TEST_ASSERT_EQUAL_HEX8(0xf7, message[13]);
  TEST_ASSERT_EQUAL_UINT32(0, buildGp5PatchStateRequest(message, 13));
}

void test_current_preset_rejects_invalid_index() {
  uint8_t payload[] = {0x12, 0x43, 6, 0};
  uint8_t preset = 0;
  TEST_ASSERT_TRUE(decodeGp5CurrentPreset(payload, sizeof(payload), preset));
  TEST_ASSERT_EQUAL_UINT8(6, preset);
  payload[2] = 100;
  TEST_ASSERT_FALSE(decodeGp5CurrentPreset(payload, sizeof(payload), preset));
}

void test_patch_names_and_effect_model_catalog() {
  uint8_t request[14] = {};
  TEST_ASSERT_EQUAL_UINT32(14,
      buildGp5PatchListRequest(request, sizeof(request)));
  TEST_ASSERT_EQUAL_HEX8(0xf0, request[0]);
  TEST_ASSERT_EQUAL_HEX8(0xf7, request[13]);
  uint8_t payload[100] = {};
  memcpy(payload + 6, "Clean One", 9);
  memcpy(payload + 26, "Crunch Two", 10);
  char names[5][11] = {};
  TEST_ASSERT_TRUE(decodeGp5PatchNames(0, payload, sizeof(payload), names));
  TEST_ASSERT_EQUAL_STRING("Clean One", names[0]);
  TEST_ASSERT_EQUAL_STRING("Crunch Two", names[1]);
  TEST_ASSERT_FALSE(decodeGp5PatchNames(20, payload, sizeof(payload), names));
  TEST_ASSERT_EQUAL_STRING("Overdrive", gp5EffectModelName(2, 0x00000003));
  TEST_ASSERT_NULL(gp5EffectModelName(2, 0xffffffff));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_read_only_request_and_bounds);
  RUN_TEST(test_current_preset_rejects_invalid_index);
  RUN_TEST(test_patch_names_and_effect_model_catalog);
  return UNITY_END();
}
