#include <unity.h>

#include "platform/ui_ble_frames.h"
#include "ui/live_json.h"

#include <cstring>

using namespace stomp::ble_ui;

void test_ingress_bounds_queue_and_correlates_absolute_commands();
void test_ingress_rejects_duplicate_stale_epoch_and_generation_and_cancels();
void test_result_history_correlates_failures_without_changing_certainty();
void test_result_history_overflow_is_bounded_and_reports_gap();
void test_json_negotiation_and_strict_command_decode();
void test_json_snapshot_and_correlated_result_encoding_are_bounded();

void test_default_att_payload_fragments_and_reassembles() {
  uint8_t source[25];
  for (uint8_t i = 0; i < sizeof(source); ++i) source[i] = i;
  TEST_ASSERT_EQUAL_UINT32(4, fragmentCount(sizeof(source), 20));
  Reassembler receiver;
  Message result;
  Frame frame;
  for (size_t i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL(FrameError::None,
                     encodeFragment(MessageType::Snapshot, 9, source,
                                    sizeof(source), i, 20, frame));
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(20, frame.length);
    const FrameError status = receiver.accept(frame.bytes, frame.length, 20,
                                              static_cast<uint32_t>(i), result);
    TEST_ASSERT_EQUAL(i == 3 ? FrameError::Complete : FrameError::Incomplete,
                      status);
  }
  TEST_ASSERT_EQUAL_UINT16(sizeof(source), result.length);
  TEST_ASSERT_EQUAL_UINT16(9, result.transfer);
  TEST_ASSERT_EQUAL_UINT8(MessageType::Snapshot, result.type);
  TEST_ASSERT_EQUAL_MEMORY(source, result.bytes, sizeof(source));

  // The peer may choose 20-byte writes even after negotiating a larger MTU.
  Reassembler wideMtuReceiver;
  for (size_t i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL(FrameError::None,
                     encodeFragment(MessageType::Snapshot, 10, source,
                                    sizeof(source), i, 20, frame));
    const FrameError status = wideMtuReceiver.accept(
        frame.bytes, frame.length, 514, static_cast<uint32_t>(i), result);
    TEST_ASSERT_EQUAL(i == 3 ? FrameError::Complete : FrameError::Incomplete,
                      status);
  }
  TEST_ASSERT_EQUAL_MEMORY(source, result.bytes, sizeof(source));
}

void test_larger_att_payload_and_size_boundaries() {
  TEST_ASSERT_EQUAL_UINT32(1, fragmentCount(1, 185));
  TEST_ASSERT_EQUAL_UINT32(17, fragmentCount(kMaxMessage, 514));
  TEST_ASSERT_EQUAL_UINT32(1024, fragmentCount(kMaxMessage, 20));
  TEST_ASSERT_EQUAL_UINT32(0, fragmentCount(kMaxMessage + 1, 20));
  uint8_t source[kMaxMessage + 1] = {};
  Frame frame;
  TEST_ASSERT_EQUAL(FrameError::TooLarge,
                   encodeFragment(MessageType::Command, 1, source,
                                  sizeof(source), 0, 20, frame));
  TEST_ASSERT_EQUAL(FrameError::TooLarge,
                   encodeFragment(MessageType::Command, 1, source, 1, 0,
                                  7, frame));
  TEST_ASSERT_EQUAL(FrameError::None,
                   encodeFragment(MessageType::Command, 1, source, 1, 0,
                                  514, frame));
  Reassembler receiver;
  Message result;
  TEST_ASSERT_EQUAL(FrameError::Complete,
                   receiver.accept(frame.bytes, frame.length, 514, 0, result));
}

void test_malformed_version_missing_duplicate_and_reordered_fragments() {
  uint8_t source[25] = {};
  Frame first, second, third;
  TEST_ASSERT_EQUAL(FrameError::None, encodeFragment(MessageType::Command, 4,
      source, sizeof(source), 0, 20, first));
  TEST_ASSERT_EQUAL(FrameError::None, encodeFragment(MessageType::Command, 4,
      source, sizeof(source), 1, 20, second));
  TEST_ASSERT_EQUAL(FrameError::None, encodeFragment(MessageType::Command, 4,
      source, sizeof(source), 3, 20, third));
  Message result;
  Reassembler receiver;
  TEST_ASSERT_EQUAL(FrameError::OutOfOrder,
                   receiver.accept(second.bytes, second.length, 20, 0, result));
  TEST_ASSERT_EQUAL(FrameError::Incomplete,
                   receiver.accept(first.bytes, first.length, 20, 1, result));
  TEST_ASSERT_EQUAL(FrameError::Duplicate,
                   receiver.accept(first.bytes, first.length, 20, 2, result));
  TEST_ASSERT_FALSE(receiver.active());
  TEST_ASSERT_EQUAL(FrameError::OutOfOrder,
                   receiver.accept(third.bytes, third.length, 20, 3, result));
  uint8_t bad[kMaxAttPayload] = {};
  memcpy(bad, first.bytes, first.length);
  bad[2] = kProtocolVersion + 1;
  TEST_ASSERT_EQUAL(FrameError::VersionMismatch,
                   receiver.accept(bad, first.length, 20, 4, result));
  memcpy(bad, first.bytes, first.length);
  bad[0] = 0;
  TEST_ASSERT_EQUAL(FrameError::Malformed,
                   receiver.accept(bad, first.length, 20, 5, result));
  TEST_ASSERT_EQUAL(FrameError::Incomplete,
                   receiver.accept(first.bytes, first.length, 20, 6, result));
  TEST_ASSERT_EQUAL(FrameError::Timeout,
                   receiver.accept(second.bytes, second.length, 20,
                                   6 + kFragmentTimeoutMs + 1, result));
  TEST_ASSERT_EQUAL(FrameError::Incomplete,
                   receiver.accept(first.bytes, first.length, 20,
                                   8 + kFragmentTimeoutMs, result));
}

void test_zero_length_and_bad_header_boundaries() {
  Frame empty;
  Message result;
  Reassembler receiver;
  TEST_ASSERT_EQUAL(FrameError::None,
                   encodeFragment(MessageType::Hello, 0, nullptr, 0, 0, 20, empty));
  TEST_ASSERT_EQUAL(FrameError::Complete,
                   receiver.accept(empty.bytes, empty.length, 20, 0, result));
  TEST_ASSERT_EQUAL_UINT16(0, result.length);
  TEST_ASSERT_EQUAL(FrameError::Malformed,
                   receiver.accept(empty.bytes, 7, 20, 1, result));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_default_att_payload_fragments_and_reassembles);
  RUN_TEST(test_larger_att_payload_and_size_boundaries);
  RUN_TEST(test_malformed_version_missing_duplicate_and_reordered_fragments);
  RUN_TEST(test_zero_length_and_bad_header_boundaries);
  RUN_TEST(test_ingress_bounds_queue_and_correlates_absolute_commands);
  RUN_TEST(test_ingress_rejects_duplicate_stale_epoch_and_generation_and_cancels);
  RUN_TEST(test_result_history_correlates_failures_without_changing_certainty);
  RUN_TEST(test_result_history_overflow_is_bounded_and_reports_gap);
  RUN_TEST(test_json_negotiation_and_strict_command_decode);
  RUN_TEST(test_json_snapshot_and_correlated_result_encoding_are_bounded);
  return UNITY_END();
}
