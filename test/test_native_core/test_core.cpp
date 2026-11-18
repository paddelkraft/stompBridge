#include <unity.h>

#include "core/bounded_queue.h"
#include "core/owned_ingress.h"
#include "core/preset_label.h"
#include "core/read_model.h"
#include "core/toggle_policy.h"

using namespace stomp;

void test_queue_is_bounded_and_fifo() {
  BoundedQueue<int, 2> queue;
  int value = 0;
  TEST_ASSERT_TRUE(queue.push(1));
  TEST_ASSERT_TRUE(queue.push(2));
  TEST_ASSERT_FALSE(queue.push(3));
  TEST_ASSERT_EQUAL_UINT32(2, queue.size());
  TEST_ASSERT_TRUE(queue.pop(value));
  TEST_ASSERT_EQUAL_INT(1, value);
  TEST_ASSERT_TRUE(queue.push(3));
  TEST_ASSERT_TRUE(queue.pop(value));
  TEST_ASSERT_EQUAL_INT(2, value);
  TEST_ASSERT_TRUE(queue.pop(value));
  TEST_ASSERT_EQUAL_INT(3, value);
  TEST_ASSERT_FALSE(queue.pop(value));
}

void test_disconnect_invalidates_certainty_and_changes_revision() {
  ReadModel model;
  DeviceCapabilities capabilities;
  capabilities.presetCount = 100;
  capabilities.blockCount = 1;
  capabilities.blocks[0].id = 50;
  model.setConnection(ConnectionPhase::Identifying, 1);
  TEST_ASSERT_TRUE(model.setCapabilities(capabilities));
  const uint32_t capabilityRevision = model.snapshot().revision;
  TEST_ASSERT_TRUE(model.setCapabilities(capabilities));
  TEST_ASSERT_EQUAL_UINT32(capabilityRevision, model.snapshot().revision);
  model.setConnection(ConnectionPhase::Ready, 1);
  model.setPreset(6, Certainty::Confirmed);
  TEST_ASSERT_TRUE(model.setBlock(50, true, Certainty::Confirmed));
  const uint32_t stableRevision = model.snapshot().revision;
  model.setPreset(6, Certainty::Confirmed);
  model.setBlock(50, true, Certainty::Confirmed);
  TEST_ASSERT_EQUAL_UINT32(stableRevision, model.snapshot().revision);
  model.setConnection(ConnectionPhase::Disconnected, 1);
  TEST_ASSERT_EQUAL(Certainty::Unknown, model.snapshot().preset.certainty);
  TEST_ASSERT_EQUAL(Certainty::Unknown, model.snapshot().blocks[0].enabled.certainty);
  TEST_ASSERT_TRUE(model.snapshot().revision > stableRevision);
  TEST_ASSERT_FALSE(model.setBlock(51, true, Certainty::Confirmed));
  model.setConnection(ConnectionPhase::Identifying, 2);
  TEST_ASSERT_EQUAL_UINT8(0, model.snapshot().capabilities.blockCount);
}

void test_capabilities_reject_duplicate_ids_without_mutation() {
  ReadModel model;
  DeviceCapabilities capabilities;
  capabilities.blockCount = 2;
  capabilities.blocks[0].id = 50;
  capabilities.blocks[1].id = 50;
  TEST_ASSERT_FALSE(model.setCapabilities(capabilities));
  TEST_ASSERT_EQUAL_UINT8(0, model.snapshot().capabilities.blockCount);
}

void test_ingress_copies_bytes_and_preserves_disconnect() {
  OwnedIngress ingress(7);
  uint8_t bytes[] = {0xf0, 0x01, 0xf7};
  TEST_ASSERT_TRUE(ingress.midi(7, bytes, sizeof(bytes)));
  bytes[1] = 0x02;
  MidiMessage message;
  TEST_ASSERT_TRUE(ingress.popMidi(message));
  TEST_ASSERT_EQUAL_HEX8(0x01, message.bytes[1]);
  for (size_t i = 0; i < kMidiIngressCapacity; ++i)
    TEST_ASSERT_TRUE(ingress.midi(7, bytes, sizeof(bytes)));
  TEST_ASSERT_FALSE(ingress.midi(7, bytes, sizeof(bytes)));
  TEST_ASSERT_EQUAL_UINT32(0, ingress.queuedMidi());
  ControlEvent event;
  TEST_ASSERT_TRUE(ingress.popControl(event));
  TEST_ASSERT_EQUAL(ControlEventKind::ReceiveLost, event.kind);
  TEST_ASSERT_TRUE(ingress.control({ControlEventKind::Disconnected, 7, 0, false}));
  TEST_ASSERT_TRUE(ingress.popControl(event));
  TEST_ASSERT_EQUAL(ControlEventKind::Disconnected, event.kind);
  TEST_ASSERT_FALSE(ingress.midi(7, bytes, sizeof(bytes)));
  TEST_ASSERT_EQUAL_UINT32(1, ingress.staleEvents());
  ingress.beginSession(8);
  TEST_ASSERT_FALSE(ingress.midi(7, bytes, sizeof(bytes)));
  TEST_ASSERT_TRUE(ingress.midi(8, bytes, sizeof(bytes)));
}

void test_unknown_toggle_reads_back_before_set() {
  const auto withReadback = planToggle(50, {}, true, 12, 3);
  TEST_ASSERT_EQUAL(Result::Accepted, withReadback.result);
  TEST_ASSERT_EQUAL(CommandKind::RequestReadback, withReadback.command.kind);
  TEST_ASSERT_TRUE(withReadback.awaitingReadback);
  TEST_ASSERT_EQUAL(Result::TimedOut, readbackTimeoutResult(withReadback));
  const auto noReadback = planToggle(50, {}, false, 13, 3);
  TEST_ASSERT_EQUAL(CommandKind::SetBlock, noReadback.command.kind);
  TEST_ASSERT_TRUE(noReadback.command.enabled);
  TEST_ASSERT_FALSE(noReadback.awaitingReadback);
  const auto confirmedOn = planToggle(50, {true, Certainty::Confirmed}, true, 14, 3);
  TEST_ASSERT_EQUAL(CommandKind::SetBlock, confirmedOn.command.kind);
  TEST_ASSERT_FALSE(confirmedOn.command.enabled);
}

void test_read_model_preserves_bounded_names_and_resets_on_replacement() {
  ReadModel model;
  model.setConnection(ConnectionPhase::Identifying, 1);
  DeviceCapabilities capabilities;
  capabilities.presetCount = 100;
  capabilities.blockCount = 1;
  capabilities.blocks[0].id = 2;
  TEST_ASSERT_TRUE(model.setCapabilities(capabilities));
  TEST_ASSERT_TRUE(model.setPresetName(0, "Clean One"));
  TEST_ASSERT_FALSE(model.setPresetName(100, "invalid"));
  TEST_ASSERT_TRUE(model.setBlockModel(2, 3, "Overdrive",
                                      Certainty::Confirmed));
  model.namesComplete();
  TEST_ASSERT_EQUAL_STRING("Clean One", model.snapshot().presetNames[0]);
  TEST_ASSERT_EQUAL_STRING("Overdrive", model.snapshot().blocks[0].modelName);
  TEST_ASSERT_TRUE(model.snapshot().presetNamesComplete);
  model.setConnection(ConnectionPhase::Identifying, 2);
  TEST_ASSERT_EQUAL_STRING("", model.snapshot().presetNames[0]);
  TEST_ASSERT_FALSE(model.snapshot().presetNamesComplete);
  TEST_ASSERT_EQUAL(Certainty::Unknown,
                    model.snapshot().blocks[0].modelCertainty);
}

void test_device_slot_labels_do_not_claim_name_readback() {
  DeviceCapabilities pocket;
  pocket.presetCount = 100;
  pocket.presetSlotLayout = PresetSlotLayout::PocketPresetAndEffect;
  char label[8] = {};
  TEST_ASSERT_TRUE(formatPresetSlot(pocket, 0, label, sizeof(label)));
  TEST_ASSERT_EQUAL_STRING("P01", label);
  TEST_ASSERT_TRUE(formatPresetSlot(pocket, 49, label, sizeof(label)));
  TEST_ASSERT_EQUAL_STRING("P50", label);
  TEST_ASSERT_TRUE(formatPresetSlot(pocket, 50, label, sizeof(label)));
  TEST_ASSERT_EQUAL_STRING("F01", label);
  TEST_ASSERT_TRUE(formatPresetSlot(pocket, 99, label, sizeof(label)));
  TEST_ASSERT_EQUAL_STRING("F50", label);
  TEST_ASSERT_FALSE(pocket.presetNameReadback);

  DeviceCapabilities gp5;
  gp5.presetCount = 100;
  gp5.presetNameReadback = true;
  gp5.presetSlotLayout = PresetSlotLayout::ZeroBasedP;
  TEST_ASSERT_TRUE(formatPresetSlot(gp5, 0, label, sizeof(label)));
  TEST_ASSERT_EQUAL_STRING("P00", label);
  TEST_ASSERT_TRUE(formatPresetSlot(gp5, 99, label, sizeof(label)));
  TEST_ASSERT_EQUAL_STRING("P99", label);
  TEST_ASSERT_FALSE(formatPresetSlot(gp5, 100, label, sizeof(label)));
  TEST_ASSERT_EQUAL_STRING("", label);

  ReadModel model;
  model.setConnection(ConnectionPhase::Identifying, 1);
  TEST_ASSERT_TRUE(model.setCapabilities(pocket));
  const uint32_t before = model.snapshot().revision;
  pocket.presetNameReadback = true;
  TEST_ASSERT_TRUE(model.setCapabilities(pocket));
  TEST_ASSERT_TRUE(model.snapshot().revision > before);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_queue_is_bounded_and_fifo);
  RUN_TEST(test_disconnect_invalidates_certainty_and_changes_revision);
  RUN_TEST(test_capabilities_reject_duplicate_ids_without_mutation);
  RUN_TEST(test_ingress_copies_bytes_and_preserves_disconnect);
  RUN_TEST(test_unknown_toggle_reads_back_before_set);
  RUN_TEST(test_read_model_preserves_bounded_names_and_resets_on_replacement);
  RUN_TEST(test_device_slot_labels_do_not_claim_name_readback);
  return UNITY_END();
}
