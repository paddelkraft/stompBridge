#include <unity.h>

#include "core/session_output.h"

using namespace stomp;

struct FakePort : MidiSendPort {
  bool allow = true;
  uint8_t submissions = 0;
  uint8_t lastByte = 0;
  bool submit(const uint8_t *bytes, size_t length) override {
    ++submissions;
    lastByte = length ? bytes[0] : 0;
    return allow;
  }
};

struct Events : OutputObserver {
  OutputEvent values[24] = {};
  uint8_t count = 0;
  void outputEvent(const OutputEvent &event) override {
    if (count < 24) values[count++] = event;
  }
};

OutputRequest request(OperationId id, SessionGeneration generation,
                      uint16_t pacing = 0, uint16_t timeout = 1000) {
  OutputRequest result;
  result.operation = id;
  result.generation = generation;
  result.length = 3;
  result.pacingMs = pacing;
  result.completionTimeoutMs = timeout;
  result.bytes[0] = 0xb0;
  result.bytes[1] = 50;
  result.bytes[2] = 127;
  return result;
}

void test_submission_completion_and_plugin_pacing() {
  FakePort port;
  Events events;
  SessionOutput output(port, events);
  output.begin(4);
  TEST_ASSERT_EQUAL(Result::Accepted, output.enqueue(request(1, 4, 120)));
  TEST_ASSERT_EQUAL(Result::Accepted, output.enqueue(request(2, 4)));
  output.tick(0xfffffff0u);
  TEST_ASSERT_EQUAL_UINT8(1, port.submissions);
  TEST_ASSERT_EQUAL(OperationPhase::Submitted, events.values[2].phase);
  output.completed(4, true, 0xfffffff5u);
  TEST_ASSERT_EQUAL(OperationPhase::TransportComplete, events.values[3].phase);
  output.tick(0x6cu);
  TEST_ASSERT_EQUAL_UINT8(1, port.submissions);
  output.tick(0x6du);
  TEST_ASSERT_EQUAL_UINT8(2, port.submissions);
  TEST_ASSERT_EQUAL(OperationPhase::Submitted, events.values[4].phase);
}

void test_timeout_blocks_replay_and_late_completion() {
  FakePort port;
  Events events;
  SessionOutput output(port, events);
  output.begin(5);
  output.enqueue(request(1, 5, 0, 100));
  output.enqueue(request(2, 5));
  output.tick(10);
  output.tick(110);
  TEST_ASSERT_EQUAL(Result::TimedOut, events.values[3].result);
  TEST_ASSERT_EQUAL(OperationPhase::Failed, events.values[3].phase);
  TEST_ASSERT_EQUAL_UINT32(0, output.queued());
  output.completed(5, true, 111);
  TEST_ASSERT_EQUAL_UINT32(1, output.staleCompletions());
  output.tick(200);
  TEST_ASSERT_EQUAL_UINT8(1, port.submissions);
}

void test_disconnect_drops_queue_and_failed_submission_stops() {
  FakePort port;
  Events events;
  SessionOutput output(port, events);
  output.begin(1);
  output.enqueue(request(1, 1));
  output.stop();
  TEST_ASSERT_EQUAL_UINT32(0, output.queued());
  TEST_ASSERT_EQUAL(Result::NotReady, output.enqueue(request(2, 1)));
  output.begin(2);
  port.allow = false;
  output.enqueue(request(3, 2));
  output.enqueue(request(4, 2));
  output.tick(0);
  TEST_ASSERT_EQUAL(Result::TransportFailed, events.values[3].result);
  TEST_ASSERT_EQUAL_UINT32(0, output.queued());
  TEST_ASSERT_EQUAL(Result::NotReady, output.enqueue(request(5, 2)));
}

void test_live_command_overtakes_queued_readback() {
  FakePort port;
  Events events;
  SessionOutput output(port, events);
  output.begin(6);
  auto readback = request(0x90000001u, 6);
  readback.bytes[0] = 0xf0;
  TEST_ASSERT_EQUAL(Result::Accepted, output.enqueue(readback));
  TEST_ASSERT_EQUAL(Result::Accepted, output.enqueue(request(1, 6)));
  output.tick(0);
  TEST_ASSERT_EQUAL_UINT8(0xb0, port.lastByte);
  output.completed(6, true, 1);
  output.tick(1);
  TEST_ASSERT_EQUAL_UINT8(0xf0, port.lastByte);
}

void test_background_and_readback_cannot_fill_live_reserve() {
  FakePort port;
  Events events;
  SessionOutput output(port, events);
  output.begin(7);
  for (uint32_t i = 0; i < kOutputQueueCapacity -
                                 kReservedLiveOutputSlots; ++i) {
    auto background = request(0x90000001u + i, 7);
    background.background = true;
    background.bytes[0] = 0xf0;
    TEST_ASSERT_EQUAL(Result::Accepted, output.enqueue(background));
  }
  auto readback = request(0x90000010u, 7);
  TEST_ASSERT_EQUAL(Result::QueueFull, output.enqueue(readback));
  TEST_ASSERT_EQUAL(Result::Accepted, output.enqueue(request(1, 7)));
  TEST_ASSERT_EQUAL(Result::Accepted, output.enqueue(request(2, 7)));
  TEST_ASSERT_EQUAL(Result::QueueFull, output.enqueue(request(3, 7)));
  output.tick(0);
  TEST_ASSERT_EQUAL_HEX8(0xb0, port.lastByte);
  output.completed(7, true, 1);
  output.tick(1);
  TEST_ASSERT_EQUAL_HEX8(0xb0, port.lastByte);
  output.completed(7, true, 2);
  output.tick(2);
  TEST_ASSERT_EQUAL_HEX8(0xf0, port.lastByte);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_submission_completion_and_plugin_pacing);
  RUN_TEST(test_timeout_blocks_replay_and_late_completion);
  RUN_TEST(test_disconnect_drops_queue_and_failed_submission_stops);
  RUN_TEST(test_live_command_overtakes_queued_readback);
  RUN_TEST(test_background_and_readback_cannot_fill_live_reserve);
  return UNITY_END();
}
