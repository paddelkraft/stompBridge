#include <unity.h>

#include "devices/pocket_master/protocol.h"

#include <cstring>

using namespace stomp::pocket;

uint8_t crc(const uint8_t *bytes, size_t length) {
  uint8_t value = 0;
  for (size_t i = 0; i < length; ++i) {
    value ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      value = (value & 0x80) ? uint8_t((value << 1) ^ 7) : uint8_t(value << 1);
  }
  return value;
}

size_t encodeFrame(uint8_t command, uint8_t index, const uint8_t *payload,
                   uint8_t length, uint8_t *output) {
  uint8_t body[104] = {0, command, index, length};
  memcpy(body + 4, payload, length);
  body[0] = crc(body + 1, length + 3);
  output[0] = 0xf0;
  for (size_t i = 0; i < size_t(length) + 4; ++i) {
    output[1 + i * 2] = body[i] >> 4;
    output[2 + i * 2] = body[i] & 0x0f;
  }
  output[9 + length * 2] = 0xf7;
  return 10 + length * 2;
}

void test_read_only_requests_are_exact() {
  uint8_t output[14];
  const uint8_t preset[] = {0xf0, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00,
                            0x00, 0x02, 0x01, 0x02, 0x04, 0x03, 0xf7};
  const uint8_t live[] = {0xf0, 0x00, 0x09, 0x00, 0x01, 0x00, 0x00,
                          0x00, 0x02, 0x01, 0x02, 0x04, 0x01, 0xf7};
  TEST_ASSERT_EQUAL_UINT32(14, currentPresetRequest(output, sizeof(output)));
  TEST_ASSERT_EQUAL_MEMORY(preset, output, sizeof(output));
  TEST_ASSERT_EQUAL_UINT32(14, liveStateRequest(output, sizeof(output)));
  TEST_ASSERT_EQUAL_MEMORY(live, output, sizeof(output));
  TEST_ASSERT_EQUAL_UINT32(0, liveStateRequest(output, 13));
}

void test_frame_crc_length_and_preset_range() {
  uint8_t output[32] = {};
  const uint8_t payload[] = {0x12, 0x43, 6, 0};
  const size_t length = encodeFrame(1, 0, payload, 4, output);
  Frame frame;
  TEST_ASSERT_TRUE(decodeFrame(output, length, frame));
  TEST_ASSERT_EQUAL_UINT8(1, frame.command);
  uint8_t preset = 0;
  TEST_ASSERT_TRUE(decodeCurrentPreset(frame.payload, frame.length, preset));
  TEST_ASSERT_EQUAL_UINT8(6, preset);
  output[2] ^= 1;
  TEST_ASSERT_FALSE(decodeFrame(output, length, frame));
  uint8_t badPreset[] = {0x12, 0x43, 100, 0};
  TEST_ASSERT_FALSE(decodeCurrentPreset(badPreset, sizeof(badPreset), preset));
}

void test_live_collector_and_block_bitmap() {
  uint8_t logical[kLiveStateBytes] = {};
  logical[0] = 0x12;
  logical[1] = 0x41;
  // Top-level module section (22 bytes), then one valid filler TLV.
  logical[2] = 0x02;
  logical[4] = 22;
  logical[6] = 0x01;
  logical[7] = 0x30;
  logical[8] = 4;
  logical[10] = (1u << 2) | (1u << 7);
  logical[11] = 1u << 0;
  logical[14] = 0x02;
  logical[15] = 0x30;
  logical[16] = 10;
  for (uint8_t i = 0; i < 10; ++i) logical[18 + i] = i;
  logical[28] = 0xff;
  logical[29] = 0xff;
  logical[30] = 0xbc; // 444-byte filler TLV.
  logical[31] = 0x01;

  LiveCollector collector;
  Frame frame;
  frame.command = 0x1a;
  frame.index = 1;
  frame.length = 18;
  TEST_ASSERT_FALSE(collector.accept(frame)); // Tail cannot start a response.
  for (uint8_t i = 0; i < 26; ++i) {
    frame.index = i;
    frame.length = i == 25 ? 26 : 18;
    memcpy(frame.payload, logical + i * 18, frame.length);
    const bool ready = collector.accept(frame);
    TEST_ASSERT_EQUAL(i == 25, ready);
  }
  TEST_ASSERT_EQUAL_UINT32(kLiveStateBytes, collector.length());
  LiveBlocks blocks;
  TEST_ASSERT_TRUE(decodeLiveBlocks(collector.logical(), collector.length(), blocks));
  TEST_ASSERT_TRUE(blocks.drive);
  TEST_ASSERT_TRUE(blocks.delay);
  TEST_ASSERT_TRUE(blocks.reverb);
  logical[19] = 0; // Duplicate chain slot invalidates certainty.
  TEST_ASSERT_FALSE(decodeLiveBlocks(logical, sizeof(logical), blocks));
}

void test_module_feedback_requires_known_shape() {
  uint8_t payload[10] = {0x12, 0x49, 2, 0, 0, 0, 1, 0, 0, 0};
  uint8_t module = 0;
  bool enabled = false;
  TEST_ASSERT_TRUE(decodeModuleFeedback(payload, sizeof(payload), module, enabled));
  TEST_ASSERT_EQUAL_UINT8(2, module);
  TEST_ASSERT_TRUE(enabled);
  payload[2] = 9;
  TEST_ASSERT_FALSE(decodeModuleFeedback(payload, sizeof(payload), module, enabled));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_read_only_requests_are_exact);
  RUN_TEST(test_frame_crc_length_and_preset_range);
  RUN_TEST(test_live_collector_and_block_bitmap);
  RUN_TEST(test_module_feedback_requires_known_shape);
  return UNITY_END();
}
