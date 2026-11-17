#include <unity.h>

#include "core/usb_midi_decoder.h"

using namespace stomp;

struct Capture {
  MidiMessage messages[4];
  uint8_t count = 0;
};

bool capture(const MidiMessage &message, void *context) {
  auto &output = *static_cast<Capture *>(context);
  if (output.count == 4) return false;
  output.messages[output.count++] = message;
  return true;
}

void test_fragmented_usb_packets_and_interleaved_realtime() {
  Capture output;
  UsbMidiDecoder decoder(7, capture, &output);
  const uint8_t packets[] = {
      0x04, 0xf0, 0x01, 0x02, // SysEx start
      0x0f, 0xf8, 0, 0,       // Clock while SysEx is open
      0x07, 0x03, 0x04, 0xf7, // SysEx end
      0x0b, 0xb0, 50, 127,   // CC
  };
  decoder.feed(packets, 5);
  decoder.feed(packets + 5, sizeof(packets) - 5);
  TEST_ASSERT_EQUAL_UINT8(3, output.count);
  TEST_ASSERT_EQUAL_UINT16(1, output.messages[0].length);
  TEST_ASSERT_EQUAL_HEX8(0xf8, output.messages[0].bytes[0]);
  TEST_ASSERT_EQUAL_UINT16(6, output.messages[1].length);
  TEST_ASSERT_EQUAL_HEX8(0xf0, output.messages[1].bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0xf7, output.messages[1].bytes[5]);
  TEST_ASSERT_EQUAL_UINT32(7, output.messages[1].generation);
  TEST_ASSERT_EQUAL_HEX8(0xb0, output.messages[2].bytes[0]);
}

void test_receive_loss_discards_partial_sysex() {
  Capture output;
  UsbMidiDecoder decoder(1, capture, &output);
  const uint8_t start[] = {4, 0xf0, 1, 2};
  const uint8_t end[] = {7, 3, 4, 0xf7};
  decoder.feed(start, sizeof(start));
  decoder.receiveLost();
  decoder.feed(end, sizeof(end));
  TEST_ASSERT_EQUAL_UINT8(0, output.count);
  TEST_ASSERT_TRUE(decoder.malformedCount() > 0);
  TEST_ASSERT_TRUE(decoder.droppedCount() > 0);
  decoder.reset(2);
  const uint8_t cc[] = {0x0b, 0xb0, 57, 0};
  decoder.feed(cc, sizeof(cc));
  TEST_ASSERT_EQUAL_UINT8(1, output.count);
  TEST_ASSERT_EQUAL_UINT32(2, output.messages[0].generation);
}

void test_invalid_cin_resets_parser() {
  Capture output;
  UsbMidiDecoder decoder(1, capture, &output);
  const uint8_t invalid[] = {0, 0, 0, 0};
  decoder.feed(invalid, sizeof(invalid));
  TEST_ASSERT_EQUAL_UINT8(0, output.count);
  TEST_ASSERT_EQUAL_UINT32(1, decoder.malformedCount());
}

void test_sysex_limit_discards_oversized_message() {
  Capture output;
  UsbMidiDecoder decoder(1, capture, &output);
  const uint8_t start[] = {4, 0xf0, 1, 2};
  const uint8_t continuation[] = {4, 3, 4, 5};
  const uint8_t end[] = {7, 6, 7, 0xf7};
  decoder.feed(start, sizeof(start));
  for (int i = 0; i < 683; ++i)
    decoder.feed(continuation, sizeof(continuation));
  decoder.feed(end, sizeof(end));
  TEST_ASSERT_EQUAL_UINT8(0, output.count);
  TEST_ASSERT_TRUE(decoder.droppedCount() > 0);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_fragmented_usb_packets_and_interleaved_realtime);
  RUN_TEST(test_receive_loss_discards_partial_sysex);
  RUN_TEST(test_invalid_cin_resets_parser);
  RUN_TEST(test_sysex_limit_discards_oversized_message);
  return UNITY_END();
}
