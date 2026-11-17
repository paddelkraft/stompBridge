#include <unity.h>

#include "devices/gp5/readback.h"

#include <cstring>

using namespace stomp::gp5;

void seal(uint8_t *midi, size_t length) {
  uint8_t crc = 0;
  for (size_t offset = 3; offset + 1 < length - 1; offset += 2) {
    crc ^= uint8_t((midi[offset] << 4) | midi[offset + 1]);
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x07)
                         : uint8_t(crc << 1);
  }
  midi[1] = crc >> 4;
  midi[2] = crc & 0x0f;
}

void test_exact_current_preset_response() {
  uint8_t midi[] = {0xf0, 0x05, 0x0e, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04,
                    0x01, 0x02, 0x04, 0x03, 0x00, 0x06, 0x00, 0x00, 0xf7};
  uint8_t index = 0;
  TEST_ASSERT_TRUE(currentPreset(midi, sizeof(midi), index));
  TEST_ASSERT_EQUAL_UINT8(6, index);
  midi[1] = 0x06;
  midi[2] = 0x01;
  midi[14] = 0x05;
  TEST_ASSERT_TRUE(currentPreset(midi, sizeof(midi), index));
  TEST_ASSERT_EQUAL_UINT8(5, index);
  midi[1] = 0x07;
  TEST_ASSERT_FALSE(currentPreset(midi, sizeof(midi), index));
  midi[1] = 0x05;
  midi[2] = 0x0e;
  midi[14] = 0x06;
  midi[12] = 0x04;
  TEST_ASSERT_FALSE(currentPreset(midi, sizeof(midi), index));
  midi[12] = 0x03;
  midi[13] = 0x06;
  midi[14] = 0x04; // 100 is outside the supported 0..99 range.
  TEST_ASSERT_FALSE(currentPreset(midi, sizeof(midi), index));
}

void test_exact_patch_fragment_bitmap() {
  uint8_t midi[48] = {};
  midi[0] = 0xf0;
  midi[47] = 0xf7;
  const uint8_t signature[] = {0x01, 0x09, 0x00, 0x03};
  memcpy(midi + 3, signature, sizeof(signature));
  midi[7] = 1;
  midi[8] = 3;
  midi[35] = 0x08;
  midi[36] = 0x04;
  midi[37] = 0x00;
  midi[38] = 0x01;
  seal(midi, sizeof(midi));
  Blocks blocks;
  TEST_ASSERT_TRUE(patchStateFragment(midi, sizeof(midi), blocks));
  TEST_ASSERT_TRUE(blocks.drive);
  TEST_ASSERT_TRUE(blocks.delay);
  TEST_ASSERT_TRUE(blocks.reverb);
  midi[35] ^= 1;
  TEST_ASSERT_FALSE(patchStateFragment(midi, sizeof(midi), blocks));
  midi[35] ^= 1;
  midi[6] = 2;
  TEST_ASSERT_FALSE(patchStateFragment(midi, sizeof(midi), blocks));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_exact_current_preset_response);
  RUN_TEST(test_exact_patch_fragment_bitmap);
  return UNITY_END();
}
