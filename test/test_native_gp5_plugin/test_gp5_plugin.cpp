#include <unity.h>

#include "devices/gp5/plugin.h"

#include <cstring>

using namespace stomp;

struct FakePort : MidiSendPort {
  uint8_t submissions = 0;
  uint8_t last[32] = {};
  size_t lastLength = 0;
  bool submit(const uint8_t *bytes, size_t length) override {
    ++submissions;
    lastLength = length;
    if (length <= sizeof(last)) memcpy(last, bytes, length);
    return true;
  }
};

struct Forward : OutputObserver {
  gp5::Plugin *plugin = nullptr;
  uint32_t now = 0;
  void outputEvent(const OutputEvent &event) override {
    if (plugin) plugin->onOutput(event, now);
  }
};

struct Updates : DeviceUpdateSink {
  DeviceUpdate values[8] = {};
  uint8_t count = 0;
  uint8_t syncStarts = 0;
  bool reported = false;
  bool verified = false;
  Result outcome = Result::Accepted;
  void publish(const DeviceUpdate &update) override {
    if (count < 8) values[count++] = update;
  }
  void synchronizationStarted(SessionGeneration) override { ++syncStarts; }
  void synchronization(SessionGeneration, bool success, Result result) override {
    reported = true;
    verified = success;
    outcome = result;
  }
};

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

MidiMessage presetFrame(uint32_t generation, uint8_t index) {
  MidiMessage message;
  message.generation = generation;
  const uint8_t bytes[] = {0xf0, 0x05, 0x0e, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04,
                           0x01, 0x02, 0x04, 0x03, uint8_t(index >> 4),
                           uint8_t(index & 0x0f), 0x00, 0x00, 0xf7};
  message.length = sizeof(bytes);
  memcpy(message.bytes, bytes, sizeof(bytes));
  seal(message.bytes, message.length);
  return message;
}

MidiMessage patchFrame(uint32_t generation) {
  MidiMessage message;
  message.generation = generation;
  message.length = 48;
  message.bytes[0] = 0xf0;
  message.bytes[47] = 0xf7;
  message.bytes[3] = 1;
  message.bytes[4] = 9;
  message.bytes[6] = 3;
  message.bytes[7] = 1;
  message.bytes[8] = 3;
  message.bytes[35] = 0x08;
  message.bytes[36] = 0x04;
  message.bytes[38] = 0x01;
  seal(message.bytes, message.length);
  return message;
}

void synchronize(gp5::Plugin &plugin, SessionOutput &output,
                 SessionGeneration generation) {
  plugin.start(generation, 0);
  output.tick(0);
  plugin.onMidi(presetFrame(generation, 6), 1);
  output.completed(generation, true, 1);
  output.tick(101);
  plugin.onMidi(patchFrame(generation), 102);
  output.completed(generation, true, 102);
  output.tick(202);
  plugin.onMidi(presetFrame(generation, 6), 203);
  output.completed(generation, true, 204);
}

void test_ready_requires_exact_two_reads_and_patch_bitmap() {
  FakePort port;
  Forward events;
  Updates updates;
  SessionOutput output(port, events);
  gp5::Plugin plugin(output, updates);
  events.plugin = &plugin;
  output.begin(2);
  TEST_ASSERT_EQUAL(Result::Accepted, plugin.start(2, 0));
  TEST_ASSERT_TRUE(plugin.readyForCommands());
  output.tick(0);
  plugin.onMidi(presetFrame(2, 6), 1);
  output.completed(2, true, 1);
  output.tick(100);
  TEST_ASSERT_EQUAL_UINT8(1, port.submissions);
  output.tick(101);
  TEST_ASSERT_EQUAL_UINT8(2, port.submissions);
  plugin.onMidi(patchFrame(2), 102);
  output.completed(2, true, 102);
  output.tick(202);
  plugin.onMidi(presetFrame(2, 6), 203);
  TEST_ASSERT_TRUE(plugin.readyForCommands());
  TEST_ASSERT_TRUE(updates.reported);
  TEST_ASSERT_TRUE(updates.verified);
  TEST_ASSERT_EQUAL_UINT8(4, updates.count);
  TEST_ASSERT_EQUAL_UINT16(6, updates.values[0].target);
  TEST_ASSERT_EQUAL(Certainty::Confirmed, updates.values[3].certainty);
  TEST_ASSERT_FALSE(plugin.capabilities().tunerReadback);

  output.completed(2, true, 204);
  Command select{CommandKind::SelectPreset, 1, false, 7, 2};
  TEST_ASSERT_EQUAL(Result::Accepted, plugin.command(select, 204));
  output.tick(204);
  TEST_ASSERT_EQUAL_UINT32(14, port.lastLength);
  output.tick(304);
  TEST_ASSERT_EQUAL_UINT32(22, port.lastLength);
  TEST_ASSERT_EQUAL_HEX8(0xf0, port.last[0]);
  TEST_ASSERT_EQUAL_HEX8(0xf7, port.last[21]);
  TEST_ASSERT_EQUAL_UINT8(4, updates.count);
  events.now = 305;
  output.completed(2, true, 305);
  TEST_ASSERT_EQUAL_UINT8(5, updates.count);
  TEST_ASSERT_EQUAL(Certainty::Assumed, updates.values[4].certainty);
  TEST_ASSERT_TRUE(plugin.readyForCommands());
  TEST_ASSERT_EQUAL_UINT8(2, updates.syncStarts);
}

void test_mismatch_and_unknown_frames_do_not_confirm() {
  FakePort port;
  Forward events;
  Updates updates;
  SessionOutput output(port, events);
  gp5::Plugin plugin(output, updates);
  events.plugin = &plugin;
  output.begin(3);
  plugin.start(3, 0);
  output.tick(0);
  plugin.onMidi(presetFrame(3, 6), 1);
  output.completed(3, true, 1);
  output.tick(101);
  MidiMessage bad = patchFrame(3);
  bad.bytes[6] = 2;
  plugin.onMidi(bad, 102);
  TEST_ASSERT_TRUE(plugin.readyForCommands());
  plugin.onMidi(patchFrame(3), 103);
  output.completed(3, true, 103);
  output.tick(203);
  plugin.onMidi(presetFrame(3, 7), 204);
  TEST_ASSERT_TRUE(plugin.readyForCommands());
  TEST_ASSERT_TRUE(updates.reported);
  TEST_ASSERT_FALSE(updates.verified);
  TEST_ASSERT_EQUAL(Result::Invalid, updates.outcome);
  TEST_ASSERT_EQUAL_UINT8(0, updates.count);
}

void test_sync_timeout_keeps_gp5_unavailable() {
  FakePort port;
  Forward events;
  Updates updates;
  SessionOutput output(port, events);
  gp5::Plugin plugin(output, updates);
  events.plugin = &plugin;
  output.begin(4);
  plugin.start(4, 0);
  output.tick(0);
  plugin.tick(4999);
  TEST_ASSERT_FALSE(updates.reported);
  plugin.tick(5000);
  TEST_ASSERT_TRUE(updates.reported);
  TEST_ASSERT_FALSE(updates.verified);
  TEST_ASSERT_EQUAL(Result::TimedOut, updates.outcome);
  TEST_ASSERT_TRUE(plugin.readyForCommands());
}

void test_live_cc_mappings_and_tuner_assumption() {
  struct Case {
    CommandKind kind;
    uint16_t target;
    uint8_t control;
  };
  const Case cases[] = {
      {CommandKind::SetBlock, 2, 50},
      {CommandKind::SetBlock, 7, 56},
      {CommandKind::SetBlock, 8, 57},
      {CommandKind::SetTuner, 0, 58},
  };
  for (const Case &item : cases) {
    FakePort port;
    Forward events;
    Updates updates;
    SessionOutput output(port, events);
    gp5::Plugin plugin(output, updates);
    events.plugin = &plugin;
    output.begin(8);
    synchronize(plugin, output, 8);
    TEST_ASSERT_TRUE(plugin.readyForCommands());
    Command toggle{CommandKind::ToggleBlock, item.target, false, 12, 8};
    TEST_ASSERT_EQUAL(Result::Unsupported, plugin.command(toggle, 204));
    Command command{item.kind, item.target, true, 13, 8};
    TEST_ASSERT_EQUAL(Result::Accepted, plugin.command(command, 204));
    output.tick(304);
    TEST_ASSERT_EQUAL_UINT32(3, port.lastLength);
    TEST_ASSERT_EQUAL_UINT8(item.control, port.last[1]);
    TEST_ASSERT_EQUAL_UINT8(127, port.last[2]);
    TEST_ASSERT_EQUAL_UINT8(4, updates.count);
    events.now = 305;
    output.completed(8, true, 305);
    TEST_ASSERT_EQUAL_UINT8(5, updates.count);
    TEST_ASSERT_EQUAL(Certainty::Assumed, updates.values[4].certainty);
    if (item.kind == CommandKind::SetTuner) {
      TEST_ASSERT_EQUAL_UINT8(1, updates.syncStarts);
      TEST_ASSERT_TRUE(plugin.readyForCommands());
    } else {
      TEST_ASSERT_EQUAL_UINT8(2, updates.syncStarts);
      TEST_ASSERT_TRUE(plugin.readyForCommands());
    }
  }
}

void test_real_gp5_name_dump_requires_all_fragments() {
  struct NameUpdates : Updates {
    uint8_t names = 0;
    bool complete = false;
    char first[3][24] = {};
    void publish(const DeviceUpdate &update) override {
      if (update.kind == DeviceUpdateKind::PresetName) {
        ++names;
        if (update.target < 3)
          memcpy(first[update.target], update.name, sizeof(update.name));
      } else if (update.kind == DeviceUpdateKind::NamesComplete) {
        complete = true;
      }
    }
  } updates;
  FakePort port;
  Forward events;
  SessionOutput output(port, events);
  gp5::Plugin plugin(output, updates);
  events.plugin = &plugin;
  output.begin(9);
  synchronize(plugin, output, 9);

  uint8_t logical[2002] = {0x12, 0x40};
  const char *expected[] = {"GreatPedal", "Neo Soul", "Star Night"};
  for (uint8_t i = 0; i < 100; ++i) {
    uint8_t *record = logical + 2 + size_t(i) * 20;
    record[0] = i;
    if (i < 3) memcpy(record + 4, expected[i], strlen(expected[i]));
  }
  auto fragment = [&](uint8_t index) {
    MidiMessage message;
    message.generation = 9;
    const uint8_t length = index == 105 ? 7 : 19;
    message.length = 10 + size_t(length) * 2;
    message.bytes[0] = 0xf0;
    message.bytes[3] = 6;
    message.bytes[4] = 10;
    message.bytes[5] = index >> 4;
    message.bytes[6] = index & 15;
    message.bytes[7] = length >> 4;
    message.bytes[8] = length & 15;
    for (uint8_t i = 0; i < length; ++i) {
      const uint8_t byte = logical[size_t(index) * 19 + i];
      message.bytes[9 + size_t(i) * 2] = byte >> 4;
      message.bytes[10 + size_t(i) * 2] = byte & 15;
    }
    message.bytes[message.length - 1] = 0xf7;
    seal(message.bytes, message.length);
    return message;
  };
  for (uint8_t index = 0; index < 106; ++index)
    if (index != 1) plugin.onMidi(fragment(index), 205 + index);
  TEST_ASSERT_FALSE(updates.complete);
  TEST_ASSERT_EQUAL_UINT8(0, updates.names);
  plugin.onMidi(fragment(1), 312);
  TEST_ASSERT_TRUE(updates.complete);
  TEST_ASSERT_EQUAL_UINT8(100, updates.names);
  TEST_ASSERT_EQUAL_STRING("GreatPedal", updates.first[0]);
  TEST_ASSERT_EQUAL_STRING("Neo Soul", updates.first[1]);
  TEST_ASSERT_EQUAL_STRING("Star Night", updates.first[2]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_ready_requires_exact_two_reads_and_patch_bitmap);
  RUN_TEST(test_mismatch_and_unknown_frames_do_not_confirm);
  RUN_TEST(test_sync_timeout_keeps_gp5_unavailable);
  RUN_TEST(test_live_cc_mappings_and_tuner_assumption);
  RUN_TEST(test_real_gp5_name_dump_requires_all_fragments);
  return UNITY_END();
}
