#include <unity.h>

#include "devices/pocket_master/plugin.h"

#include <cstring>

using namespace stomp;

struct FakePort : MidiSendPort {
  uint8_t submissions = 0;
  uint8_t last[3] = {};
  bool allow = true;
  bool submit(const uint8_t *bytes, size_t length) override {
    ++submissions;
    if (length == 3) memcpy(last, bytes, 3);
    return allow;
  }
};

struct ForwardEvents : OutputObserver {
  pocket::Plugin *plugin = nullptr;
  uint32_t now = 0;
  void outputEvent(const OutputEvent &event) override {
    if (plugin) plugin->onOutput(event, now);
  }
};

struct Updates : DeviceUpdateSink {
  DeviceUpdate values[8] = {};
  uint8_t count = 0;
  bool syncReported = false;
  bool syncVerified = false;
  Result syncOutcome = Result::Accepted;
  uint8_t syncStarts = 0;
  void publish(const DeviceUpdate &update) override {
    if (count < 8) values[count++] = update;
  }
  void synchronization(SessionGeneration, bool verified, Result outcome) override {
    syncReported = true;
    syncVerified = verified;
    syncOutcome = outcome;
  }
  void synchronizationStarted(SessionGeneration) override { ++syncStarts; }
};

uint8_t crc(const uint8_t *bytes, size_t length) {
  uint8_t value = 0;
  for (size_t i = 0; i < length; ++i) {
    value ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      value = (value & 0x80) ? uint8_t((value << 1) ^ 7) : uint8_t(value << 1);
  }
  return value;
}

MidiMessage frame(uint32_t generation, uint8_t command, uint8_t index,
                  const uint8_t *payload, uint8_t length) {
  MidiMessage message;
  message.generation = generation;
  uint8_t body[104] = {0, command, index, length};
  memcpy(body + 4, payload, length);
  body[0] = crc(body + 1, length + 3);
  message.bytes[0] = 0xf0;
  for (size_t i = 0; i < size_t(length) + 4; ++i) {
    message.bytes[1 + i * 2] = body[i] >> 4;
    message.bytes[2 + i * 2] = body[i] & 0x0f;
  }
  message.length = 10 + length * 2;
  message.bytes[message.length - 1] = 0xf7;
  return message;
}

void liveLogical(uint8_t *logical) {
  memset(logical, 0, pocket::kLiveStateBytes);
  logical[0] = 0x12;
  logical[1] = 0x41;
  logical[2] = 2;
  logical[4] = 22;
  logical[6] = 1;
  logical[7] = 0x30;
  logical[8] = 4;
  logical[10] = (1u << 2) | (1u << 7);
  logical[11] = 1;
  logical[14] = 2;
  logical[15] = 0x30;
  logical[16] = 10;
  for (uint8_t i = 0; i < 10; ++i) logical[18 + i] = i;
  logical[28] = logical[29] = 0xff;
  logical[30] = 0xbc;
  logical[31] = 1;
}

void test_ready_requires_coordinated_verified_readback() {
  FakePort port;
  ForwardEvents events;
  Updates updates;
  SessionOutput output(port, events);
  pocket::Plugin plugin(output, updates);
  events.plugin = &plugin;
  output.begin(4);
  TEST_ASSERT_EQUAL(Result::Accepted, plugin.start(4, 0));
  const uint8_t preset[] = {0x12, 0x43, 6, 0};
  const MidiMessage first = frame(4, 1, 0, preset, sizeof(preset));
  plugin.onMidi(first, 0); // Unsolicited before submission is ignored.
  TEST_ASSERT_EQUAL_UINT32(1, output.queued());
  output.tick(0);
  plugin.onMidi(first, 1);
  TEST_ASSERT_TRUE(plugin.readyForCommands());
  output.completed(4, true, 1);
  output.tick(1);
  uint8_t logical[pocket::kLiveStateBytes];
  liveLogical(logical);
  for (uint8_t i = 0; i < 26; ++i) {
    const uint8_t length = i == 25 ? 26 : 18;
    plugin.onMidi(frame(4, 0x1a, i, logical + i * 18, length), 2 + i);
  }
  TEST_ASSERT_TRUE(plugin.readyForCommands());
  output.completed(4, true, 30);
  output.tick(30);
  plugin.onMidi(first, 31);
  TEST_ASSERT_TRUE(plugin.readyForCommands());
  TEST_ASSERT_TRUE(updates.syncReported);
  TEST_ASSERT_TRUE(updates.syncVerified);
  TEST_ASSERT_EQUAL_UINT8(4, updates.count);
  TEST_ASSERT_EQUAL_UINT16(6, updates.values[0].target);
  TEST_ASSERT_EQUAL(Certainty::Confirmed, updates.values[0].certainty);
  TEST_ASSERT_EQUAL_UINT16(8, updates.values[3].target);
  TEST_ASSERT_TRUE(updates.values[3].enabled);
  TEST_ASSERT_FALSE(plugin.capabilities().tunerReadback);
  output.completed(4, true, 32);
  Command toggle{CommandKind::ToggleBlock, 2, false, 12, 4};
  TEST_ASSERT_EQUAL(Result::Unsupported, plugin.command(toggle, 32));
  Command setDrive{CommandKind::SetBlock, 2, false, 13, 4};
  TEST_ASSERT_EQUAL(Result::Accepted, plugin.command(setDrive, 32));
  output.tick(32);
  TEST_ASSERT_EQUAL_HEX8(0xb0, port.last[0]);
  TEST_ASSERT_EQUAL_UINT8(45, port.last[1]);
  TEST_ASSERT_EQUAL_UINT8(0, port.last[2]);
  TEST_ASSERT_EQUAL_UINT8(4, updates.count); // Submission is not observation.
  events.now = 40;
  output.completed(4, true, 40);
  TEST_ASSERT_EQUAL_UINT8(5, updates.count);
  TEST_ASSERT_EQUAL(Certainty::Assumed, updates.values[4].certainty);
  TEST_ASSERT_FALSE(updates.values[4].enabled);
  TEST_ASSERT_TRUE(plugin.readyForCommands()); // Readback was scheduled.
  TEST_ASSERT_EQUAL_UINT8(2, updates.syncStarts);
  TEST_ASSERT_EQUAL_UINT32(1, output.queued());
}

void test_preset_change_during_sync_fails() {
  FakePort port;
  ForwardEvents events;
  Updates updates;
  SessionOutput output(port, events);
  pocket::Plugin plugin(output, updates);
  events.plugin = &plugin;
  output.begin(7);
  plugin.start(7, 0);
  output.tick(0);
  const uint8_t first[] = {0x12, 0x43, 6, 0};
  plugin.onMidi(frame(7, 1, 0, first, sizeof(first)), 1);
  output.completed(7, true, 1);
  output.tick(1);
  uint8_t logical[pocket::kLiveStateBytes];
  liveLogical(logical);
  for (uint8_t i = 0; i < 26; ++i)
    plugin.onMidi(frame(7, 0x1a, i, logical + i * 18, i == 25 ? 26 : 18), 2 + i);
  output.completed(7, true, 30);
  output.tick(30);
  const uint8_t second[] = {0x12, 0x43, 7, 0};
  plugin.onMidi(frame(7, 1, 0, second, sizeof(second)), 31);
  TEST_ASSERT_TRUE(plugin.readyForCommands());
  TEST_ASSERT_TRUE(updates.syncReported);
  TEST_ASSERT_FALSE(updates.syncVerified);
  TEST_ASSERT_EQUAL(Result::Invalid, updates.syncOutcome);
  TEST_ASSERT_EQUAL_UINT8(0, updates.count);
}

void test_missing_response_times_out_without_ready() {
  FakePort port;
  ForwardEvents events;
  Updates updates;
  SessionOutput output(port, events);
  pocket::Plugin plugin(output, updates);
  events.plugin = &plugin;
  output.begin(9);
  plugin.start(9, 0);
  output.tick(0);
  plugin.tick(1999);
  TEST_ASSERT_FALSE(updates.syncReported);
  plugin.tick(2000);
  TEST_ASSERT_TRUE(updates.syncReported);
  TEST_ASSERT_FALSE(updates.syncVerified);
  TEST_ASSERT_EQUAL(Result::TimedOut, updates.syncOutcome);
  TEST_ASSERT_TRUE(plugin.readyForCommands());
}

void test_transport_failure_during_sync_never_promotes_ready() {
  FakePort port;
  port.allow = false;
  ForwardEvents events;
  Updates updates;
  SessionOutput output(port, events);
  pocket::Plugin plugin(output, updates);
  events.plugin = &plugin;
  output.begin(10);
  TEST_ASSERT_EQUAL(Result::Accepted, plugin.start(10, 0));
  output.tick(0);
  TEST_ASSERT_TRUE(updates.syncReported);
  TEST_ASSERT_FALSE(updates.syncVerified);
  TEST_ASSERT_EQUAL(Result::TransportFailed, updates.syncOutcome);
  TEST_ASSERT_TRUE(plugin.readyForCommands());
  TEST_ASSERT_EQUAL_UINT8(0, updates.count);
}

void test_tuner_state_only_becomes_assumed_after_completion() {
  FakePort port;
  ForwardEvents events;
  Updates updates;
  SessionOutput output(port, events);
  pocket::Plugin plugin(output, updates);
  events.plugin = &plugin;
  output.begin(11);
  plugin.start(11, 0);
  output.tick(0);
  const uint8_t preset[] = {0x12, 0x43, 6, 0};
  plugin.onMidi(frame(11, 1, 0, preset, sizeof(preset)), 1);
  output.completed(11, true, 1);
  output.tick(1);
  uint8_t logical[pocket::kLiveStateBytes];
  liveLogical(logical);
  for (uint8_t i = 0; i < 26; ++i)
    plugin.onMidi(frame(11, 0x1a, i, logical + i * 18, i == 25 ? 26 : 18), 2 + i);
  output.completed(11, true, 30);
  output.tick(30);
  plugin.onMidi(frame(11, 1, 0, preset, sizeof(preset)), 31);
  output.completed(11, true, 32);
  Command tuner{CommandKind::SetTuner, 0, true, 17, 11};
  TEST_ASSERT_EQUAL(Result::Accepted, plugin.command(tuner, 32));
  output.tick(32);
  TEST_ASSERT_EQUAL_UINT8(58, port.last[1]);
  TEST_ASSERT_EQUAL_UINT8(127, port.last[2]);
  TEST_ASSERT_EQUAL_UINT8(4, updates.count);
  output.completed(11, true, 40);
  TEST_ASSERT_EQUAL_UINT8(5, updates.count);
  TEST_ASSERT_EQUAL(DeviceUpdateKind::Tuner, updates.values[4].kind);
  TEST_ASSERT_EQUAL(Certainty::Assumed, updates.values[4].certainty);
  TEST_ASSERT_EQUAL_UINT8(1, updates.syncStarts);
  TEST_ASSERT_TRUE(plugin.readyForCommands());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_ready_requires_coordinated_verified_readback);
  RUN_TEST(test_preset_change_during_sync_fails);
  RUN_TEST(test_missing_response_times_out_without_ready);
  RUN_TEST(test_transport_failure_during_sync_never_promotes_ready);
  RUN_TEST(test_tuner_state_only_becomes_assumed_after_completion);
  return UNITY_END();
}
