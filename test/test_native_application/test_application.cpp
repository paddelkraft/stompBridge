#include <unity.h>

#include "core/application.h"

#include <cstring>

using namespace stomp;

struct FakeClock : Clock {
  uint32_t now = 0;
  uint32_t nowMs() const override { return now; }
};

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

MidiMessage preset(uint8_t index) {
  MidiMessage message;
  const uint8_t bytes[] = {0xf0, 0x05, 0x0e, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04,
                           0x01, 0x02, 0x04, 0x03, uint8_t(index >> 4),
                           uint8_t(index & 0x0f), 0x00, 0x00, 0xf7};
  message.length = sizeof(bytes);
  memcpy(message.bytes, bytes, sizeof(bytes));
  return message;
}

MidiMessage patch() {
  MidiMessage message;
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
  message.bytes[38] = 1;
  seal(message.bytes, message.length);
  return message;
}

void post(Application &app, const MidiMessage &message) {
  TEST_ASSERT_TRUE(app.postMidi(app.generation(), message.bytes, message.length));
  app.pump();
}

void complete(Application &app) {
  TEST_ASSERT_TRUE(app.postControl(
      {ControlEventKind::SendComplete, app.generation(), 0, true}));
  app.pump();
}

void synchronizeGp5(Application &app, FakeClock &clock, uint32_t start) {
  clock.now = start;
  app.pump();
  clock.now = start + 1;
  post(app, preset(6));
  complete(app);
  clock.now = start + 101;
  app.pump();
  clock.now = start + 102;
  post(app, patch());
  complete(app);
  clock.now = start + 202;
  app.pump();
  clock.now = start + 203;
  post(app, preset(6));
}

void test_attach_sync_and_confirmed_toggle_resync() {
  FakeClock clock;
  FakePort port;
  Application app(clock, port);
  app.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0184, 0});
  TEST_ASSERT_EQUAL(ConnectionPhase::Synchronizing, app.snapshot().connection);
  TEST_ASSERT_EQUAL(SyncState::Pending, app.snapshot().sync);
  TEST_ASSERT_EQUAL(ConnectionPhase::Synchronizing, app.snapshot().connection);
  synchronizeGp5(app, clock, 0);
  TEST_ASSERT_EQUAL(ConnectionPhase::Ready, app.snapshot().connection);
  TEST_ASSERT_EQUAL(SyncState::Confirmed, app.snapshot().sync);
  TEST_ASSERT_EQUAL_UINT16(1, app.snapshot().localBank);
  TEST_ASSERT_EQUAL(Certainty::Confirmed,
                    app.snapshot().blocks[0].enabled.certainty);
  clock.now = 204;
  complete(app);
  clock.now = 205;
  complete(app); // Release the optional patch-name request.
  const Admission toggle = app.submit(CommandKind::ToggleBlock, 2);
  TEST_ASSERT_EQUAL(Result::Accepted, toggle.result);
  app.pump();
  TEST_ASSERT_EQUAL(ConnectionPhase::Ready, app.snapshot().connection);
  clock.now = 305;
  app.pump();
  TEST_ASSERT_EQUAL_UINT32(3, port.lastLength);
  TEST_ASSERT_EQUAL_UINT8(50, port.last[1]);
  TEST_ASSERT_EQUAL_UINT8(0, port.last[2]);
  clock.now = 306;
  complete(app);
  TEST_ASSERT_EQUAL(ConnectionPhase::Synchronizing, app.snapshot().connection);
  synchronizeGp5(app, clock, 656);
  TEST_ASSERT_EQUAL(ConnectionPhase::Ready, app.snapshot().connection);
  TEST_ASSERT_EQUAL(Certainty::Confirmed,
                    app.snapshot().blocks[0].enabled.certainty);
}

void test_live_command_dispatches_during_missing_readback() {
  FakeClock clock;
  FakePort port;
  Application app(clock, port);
  app.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0184, 0});
  const Admission command = app.submit(CommandKind::SetTuner, 0, true);
  TEST_ASSERT_EQUAL(Result::Accepted, command.result);
  app.pump();
  complete(app); // Release the initial read-only request's send buffer.
  clock.now = 100;
  app.pump();
  TEST_ASSERT_EQUAL_UINT32(3, port.lastLength);
  TEST_ASSERT_EQUAL_UINT8(58, port.last[1]);
  TEST_ASSERT_EQUAL_UINT8(127, port.last[2]);
  TEST_ASSERT_EQUAL(ConnectionPhase::Degraded, app.snapshot().connection);
  TEST_ASSERT_EQUAL(SyncState::Interrupted, app.snapshot().sync);
  TEST_ASSERT_EQUAL(Result::Interrupted, app.snapshot().syncResult);
}

void test_sync_timeout_keeps_live_commands_available() {
  FakeClock clock;
  FakePort port;
  Application app(clock, port);
  app.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0184, 0});
  app.pump();
  complete(app);
  clock.now = 5001;
  app.pump();
  TEST_ASSERT_EQUAL(ConnectionPhase::Degraded, app.snapshot().connection);
  TEST_ASSERT_EQUAL(SyncState::Failed, app.snapshot().sync);
  TEST_ASSERT_EQUAL(Result::TimedOut, app.snapshot().syncResult);
  TEST_ASSERT_EQUAL(Certainty::Unknown, app.snapshot().preset.certainty);
  TEST_ASSERT_EQUAL(Result::Accepted,
                    app.submit(CommandKind::SetTuner, 0, true).result);
  app.pump();
  TEST_ASSERT_EQUAL_UINT32(3, port.lastLength);
  TEST_ASSERT_EQUAL_UINT8(58, port.last[1]);
}

void test_successive_live_commands_wait_for_send_buffer_without_dropping() {
  FakeClock clock;
  FakePort port;
  Application app(clock, port);
  app.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0184, 0});
  synchronizeGp5(app, clock, 0);
  clock.now = 204;
  complete(app);
  const Admission first = app.submit(CommandKind::SetTuner, 0, true);
  const Admission second = app.submit(CommandKind::SetTuner, 0, false);
  TEST_ASSERT_EQUAL(Result::Accepted, first.result);
  TEST_ASSERT_EQUAL(Result::Accepted, second.result);
  app.pump();
  app.pump();
  TEST_ASSERT_EQUAL(OperationPhase::Queued, app.snapshot().lastPhase);
  clock.now = 304;
  app.pump();
  TEST_ASSERT_EQUAL_UINT8(127, port.last[2]);
  complete(app);
  clock.now = 654;
  app.pump();
  TEST_ASSERT_EQUAL_UINT8(0, port.last[2]);
}

void test_switch_binding_keeps_bank_local_and_selects_absolute_preset() {
  FakeClock clock;
  FakePort port;
  Application app(clock, port);
  TEST_ASSERT_EQUAL(Result::Accepted,
                    app.gesture({GestureKind::Press, 1u << 1, 0}).result);
  TEST_ASSERT_EQUAL_UINT16(1, app.snapshot().localBank);
  TEST_ASSERT_EQUAL(Result::NotReady,
                    app.gesture({GestureKind::Press, 1u << 5, 0}).result);
  app.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0184, 0});
  const Admission preset = app.gesture({GestureKind::Press, 1u << 7, 0});
  TEST_ASSERT_EQUAL(Result::Accepted, preset.result);
  app.pump();
  complete(app);
  clock.now = 100;
  app.pump();
  TEST_ASSERT_EQUAL_UINT32(22, port.lastLength);
  TEST_ASSERT_EQUAL_UINT8(6, port.last[14]);
  TEST_ASSERT_EQUAL(Result::Unsupported,
                    app.gesture({GestureKind::Chord, 0x3, 0}).result);
}

void test_unsupported_ble_and_local_bank_without_pedal() {
  FakeClock clock;
  FakePort port;
  Application app(clock, port);
  app.bank(1);
  TEST_ASSERT_EQUAL_UINT16(1, app.snapshot().localBank);
  app.attach({IdentityKind::BleMidiServiceOnly, 0, 0, 0});
  TEST_ASSERT_EQUAL(ConnectionPhase::Unsupported, app.snapshot().connection);
  TEST_ASSERT_EQUAL(Result::NotReady,
                    app.submit(CommandKind::SetTuner, 0, true).result);
  app.bank(-1);
  TEST_ASSERT_EQUAL_UINT16(0, app.snapshot().localBank);
  TEST_ASSERT_EQUAL_UINT8(0, port.submissions);
}

void test_receive_loss_faults_and_invalidates_certainty() {
  FakeClock clock;
  FakePort port;
  Application app(clock, port);
  app.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0184, 0});
  synchronizeGp5(app, clock, 0);
  uint8_t oversized[kMaxMidiMessage + 1] = {};
  TEST_ASSERT_FALSE(app.postMidi(app.generation(), oversized, sizeof(oversized)));
  app.pump();
  TEST_ASSERT_EQUAL(ConnectionPhase::FaultBackoff, app.snapshot().connection);
  TEST_ASSERT_EQUAL(Certainty::Unknown,
                    app.snapshot().blocks[0].enabled.certainty);
  TEST_ASSERT_EQUAL(Result::NotReady,
                    app.submit(CommandKind::SetBlock, 2, false).result);
  const uint8_t sendsBefore = port.submissions;
  const MidiMessage late = preset(6);
  TEST_ASSERT_TRUE(app.postMidi(app.generation(), late.bytes, late.length));
  app.pump();
  TEST_ASSERT_EQUAL_UINT8(sendsBefore, port.submissions);
  TEST_ASSERT_EQUAL(ConnectionPhase::FaultBackoff, app.snapshot().connection);
}

void test_replacement_drops_old_generation_events() {
  FakeClock clock;
  FakePort port;
  Application app(clock, port);
  app.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0184, 0});
  const auto oldGeneration = app.generation();
  app.pump();
  app.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0284, 0});
  const auto newGeneration = app.generation();
  TEST_ASSERT_TRUE(newGeneration != oldGeneration);
  TEST_ASSERT_EQUAL_STRING("pocket_master", app.deviceId());
  const MidiMessage old = preset(6);
  TEST_ASSERT_FALSE(app.postMidi(oldGeneration, old.bytes, old.length));
  app.disconnect(oldGeneration);
  TEST_ASSERT_EQUAL(ConnectionPhase::Synchronizing, app.snapshot().connection);
  TEST_ASSERT_EQUAL_UINT32(newGeneration, app.generation());
  app.disconnect(newGeneration);
  TEST_ASSERT_EQUAL(ConnectionPhase::Disconnected, app.snapshot().connection);
  TEST_ASSERT_EQUAL(Result::NotReady,
                    app.submit(CommandKind::SelectPreset, 1).result);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_attach_sync_and_confirmed_toggle_resync);
  RUN_TEST(test_live_command_dispatches_during_missing_readback);
  RUN_TEST(test_sync_timeout_keeps_live_commands_available);
  RUN_TEST(test_successive_live_commands_wait_for_send_buffer_without_dropping);
  RUN_TEST(test_switch_binding_keeps_bank_local_and_selects_absolute_preset);
  RUN_TEST(test_unsupported_ble_and_local_bank_without_pedal);
  RUN_TEST(test_receive_loss_faults_and_invalidates_certainty);
  RUN_TEST(test_replacement_drops_old_generation_events);
  return UNITY_END();
}
