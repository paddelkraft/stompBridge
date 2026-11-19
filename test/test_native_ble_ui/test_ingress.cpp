#include <unity.h>

#include "core/read_model.h"
#include "ui/live_ingress.h"
#include "ui/live_json.h"

#include <cstring>

using namespace stomp;
using namespace stomp::ui;

void test_ingress_bounds_queue_and_correlates_absolute_commands() {
  LiveIngress ingress;
  ingress.connect(7, 12);
  for (uint32_t id = 1; id <= kCommandQueueCapacity; ++id)
    TEST_ASSERT_EQUAL(Result::Accepted,
        ingress.enqueue(id, 7, 12, CommandKind::SelectPreset,
                        static_cast<uint16_t>(id + 5), false));
  TEST_ASSERT_EQUAL(Result::QueueFull,
      ingress.enqueue(5, 7, 12, CommandKind::SetBlock, 2, true));
  CommandRequest request;
  TEST_ASSERT_TRUE(ingress.pop(request));
  TEST_ASSERT_EQUAL_UINT32(1, request.requestId);
  TEST_ASSERT_EQUAL_UINT32(7, request.uiEpoch);
  TEST_ASSERT_EQUAL_UINT32(12, request.generation);
  TEST_ASSERT_EQUAL(CommandKind::SelectPreset, request.command.kind);
  TEST_ASSERT_EQUAL_UINT16(6, request.command.target);
  TEST_ASSERT_EQUAL_UINT32(12, request.command.generation);
}

void test_ingress_rejects_duplicate_stale_epoch_and_generation_and_cancels() {
  LiveIngress ingress;
  ingress.connect(20, 4);
  TEST_ASSERT_EQUAL(Result::Accepted,
      ingress.enqueue(1, 20, 4, CommandKind::SetBlock, 2, false));
  TEST_ASSERT_EQUAL(Result::Invalid,
      ingress.enqueue(1, 20, 4, CommandKind::SetBlock, 2, true));
  TEST_ASSERT_EQUAL(Result::Unsupported,
      ingress.enqueue(2, 20, 4, CommandKind::ToggleBlock, 2, false));
  TEST_ASSERT_EQUAL(Result::Unsupported,
      ingress.enqueue(2, 20, 4, CommandKind::ToggleTuner, 0, false));
  TEST_ASSERT_EQUAL(Result::Interrupted,
      ingress.enqueue(2, 19, 4, CommandKind::SetBlock, 2, true));
  TEST_ASSERT_EQUAL(Result::Interrupted,
      ingress.enqueue(2, 20, 3, CommandKind::SetBlock, 2, true));
  ingress.pedalGeneration(5);
  TEST_ASSERT_EQUAL_UINT32(0, ingress.queued());
  TEST_ASSERT_EQUAL(Result::Interrupted,
      ingress.enqueue(2, 20, 4, CommandKind::SetBlock, 2, true));
  TEST_ASSERT_EQUAL(Result::Accepted,
      ingress.enqueue(2, 20, 5, CommandKind::SetBlock, 2, true));
  ingress.disconnect(20);
  TEST_ASSERT_EQUAL_UINT32(0, ingress.queued());
  TEST_ASSERT_EQUAL(Result::Interrupted,
      ingress.enqueue(3, 20, 5, CommandKind::SetBlock, 2, true));
  ingress.connect(21, 5);
  TEST_ASSERT_EQUAL(Result::Interrupted,
      ingress.enqueue(3, 20, 5, CommandKind::SetBlock, 2, true));
}

void test_result_history_correlates_failures_without_changing_certainty() {
  ReadModel model;
  DeviceCapabilities caps;
  caps.blockCount = 1;
  caps.blocks[0].id = 2;
  model.setConnection(ConnectionPhase::Ready, 8);
  TEST_ASSERT_TRUE(model.setCapabilities(caps));
  model.setBlock(2, true, Certainty::Confirmed);
  const uint32_t revision = model.snapshot().revision;
  ResultLedger ledger;
  ledger.record({41, 6, 8, 19, OperationPhase::Failed, Result::TransportFailed});
  ledger.record({42, 6, 8, 20, OperationPhase::Submitted, Result::Accepted});
  RequestResult result;
  TEST_ASSERT_TRUE(ledger.find(6, 41, result));
  TEST_ASSERT_EQUAL_UINT32(19, result.operation);
  TEST_ASSERT_EQUAL(OperationPhase::Failed, result.phase);
  TEST_ASSERT_EQUAL(Result::TransportFailed, result.result);
  TEST_ASSERT_FALSE(ledger.find(7, 41, result));
  TEST_ASSERT_EQUAL_UINT32(revision, model.snapshot().revision);
  TEST_ASSERT_EQUAL(Certainty::Confirmed,
                    model.snapshot().blocks[0].enabled.certainty);
}

void test_result_history_overflow_is_bounded_and_reports_gap() {
  ResultLedger ledger;
  for (uint32_t id = 1; id <= kResultHistoryCapacity + 2; ++id)
    ledger.record({id, 9, 3, id, OperationPhase::Queued, Result::Accepted});
  TEST_ASSERT_EQUAL_UINT32(2, ledger.oldestRetainedSequence());
  TEST_ASSERT_EQUAL_UINT32(kResultHistoryCapacity + 2, ledger.nextSequence());
  TEST_ASSERT_EQUAL_UINT32(2, ledger.lost());
  RequestResult result;
  TEST_ASSERT_FALSE(ledger.find(9, 1, result));
  TEST_ASSERT_TRUE(ledger.find(9, kResultHistoryCapacity + 2, result));
}

void test_json_negotiation_and_strict_command_decode() {
  const char hello[] = "{\"kind\":\"hello\",\"max\":1,\"epoch\":3,\"v\":1,\"min\":1}";
  HelloRequest negotiated;
  TEST_ASSERT_EQUAL(JsonStatus::Ok, decodeHello(
      reinterpret_cast<const uint8_t *>(hello), sizeof(hello) - 1, negotiated));
  TEST_ASSERT_EQUAL_UINT32(3, negotiated.epoch);
  TEST_ASSERT_EQUAL_UINT8(1, negotiated.minimumVersion);
  const char wrong[] = "{\"v\":2,\"kind\":\"hello\",\"min\":2,\"max\":2,\"epoch\":3}";
  TEST_ASSERT_EQUAL(JsonStatus::VersionMismatch, decodeHello(
      reinterpret_cast<const uint8_t *>(wrong), sizeof(wrong) - 1, negotiated));
  const char invalidUtf8[] = "{\"v\":1,\"kind\":\"hello\",\"min\":1,\"max\":1,\"epoch\":3,\"x\":\"\xC0\"}";
  TEST_ASSERT_EQUAL(JsonStatus::Malformed, decodeHello(
      reinterpret_cast<const uint8_t *>(invalidUtf8), sizeof(invalidUtf8) - 1,
      negotiated));

  const char presetJson[] = "{\"v\":1,\"kind\":\"command\",\"request\":41,\"epoch\":3,\"generation\":12,\"command\":{\"kind\":\"setPreset\",\"index\":6}}";
  UiRequest request;
  TEST_ASSERT_EQUAL(JsonStatus::Ok, decodeRequest(
      reinterpret_cast<const uint8_t *>(presetJson), sizeof(presetJson) - 1, request));
  TEST_ASSERT_EQUAL(CommandKind::SelectPreset, request.command);
  TEST_ASSERT_EQUAL_UINT16(6, request.target);
  TEST_ASSERT_EQUAL_UINT32(41, request.requestId);

  const char blockJson[] = "{\"v\":1,\"kind\":\"command\",\"request\":42,\"epoch\":3,\"generation\":12,\"command\":{\"enabled\":false,\"id\":2,\"kind\":\"setBlock\"}}";
  TEST_ASSERT_EQUAL(JsonStatus::Ok, decodeRequest(
      reinterpret_cast<const uint8_t *>(blockJson), sizeof(blockJson) - 1, request));
  TEST_ASSERT_EQUAL(CommandKind::SetBlock, request.command);
  TEST_ASSERT_FALSE(request.enabled);

  const char toggleJson[] = "{\"v\":1,\"kind\":\"command\",\"request\":43,\"epoch\":3,\"generation\":12,\"command\":{\"kind\":\"toggleBlock\",\"id\":2}}";
  TEST_ASSERT_EQUAL(JsonStatus::Unsupported, decodeRequest(
      reinterpret_cast<const uint8_t *>(toggleJson), sizeof(toggleJson) - 1, request));
  const char duplicateJson[] = "{\"v\":1,\"v\":1,\"kind\":\"snapshot\",\"request\":44,\"epoch\":3,\"generation\":12}";
  TEST_ASSERT_EQUAL(JsonStatus::Malformed, decodeRequest(
      reinterpret_cast<const uint8_t *>(duplicateJson), sizeof(duplicateJson) - 1, request));
  const char missingJson[] = "{\"v\":1,\"kind\":\"command\",\"request\":45,\"epoch\":3,\"generation\":12,\"command\":{\"kind\":\"setBlock\",\"id\":2}}";
  TEST_ASSERT_EQUAL(JsonStatus::Malformed, decodeRequest(
      reinterpret_cast<const uint8_t *>(missingJson), sizeof(missingJson) - 1, request));
  uint8_t oversized[kMaxJsonCommandBytes + 1] = {};
  TEST_ASSERT_EQUAL(JsonStatus::TooLarge,
      decodeRequest(oversized, sizeof(oversized), request));
}

void test_json_snapshot_and_correlated_result_encoding_are_bounded() {
  ReadSnapshot snapshot;
  snapshot.generation = 12;
  snapshot.revision = 88;
  snapshot.connection = ConnectionPhase::Ready;
  snapshot.sync = SyncState::Confirmed;
  snapshot.capabilities.presetCount = 100;
  snapshot.capabilities.blockCount = kMaxBlocks;
  snapshot.capabilities.presetSlotLayout = PresetSlotLayout::ZeroBasedP;
  for (uint8_t i = 0; i < kMaxBlocks; ++i) {
    snapshot.capabilities.blocks[i].id = i;
    memset(snapshot.capabilities.blocks[i].label, '"', kMaxBlockLabel - 1);
    snapshot.blocks[i].id = i;
  }
  for (uint8_t i = 0; i < 4; ++i)
    memset(snapshot.presetNames[i], 1, kMaxPresetName - 1);
  char json[kMaxJsonSnapshotBytes + 1] = {};
  const size_t encoded = encodeSnapshot(snapshot, 3, true, json, sizeof(json));
  TEST_ASSERT_GREATER_THAN_UINT32(0, encoded);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(kMaxJsonSnapshotBytes, encoded);
  TEST_ASSERT_NOT_NULL(strstr(json, "\"revision\":88"));
  TEST_ASSERT_NOT_NULL(strstr(json, "\\u0001"));
  TEST_ASSERT_EQUAL_UINT32(0, encodeSnapshot(snapshot, 3, true, json, encoded));

  RequestResult result{41, 3, 12, 19, OperationPhase::Failed,
                       Result::TransportFailed, Result::Accepted};
  char resultJson[257] = {};
  const size_t resultSize = encodeResult(result, resultJson, sizeof(resultJson));
  TEST_ASSERT_GREATER_THAN_UINT32(0, resultSize);
  TEST_ASSERT_NOT_NULL(strstr(resultJson, "\"admission\":\"accepted\""));
  TEST_ASSERT_NOT_NULL(strstr(resultJson, "\"result\":\"transportFailed\""));
  TEST_ASSERT_EQUAL_UINT32(0, encodeResult(result, resultJson, resultSize));
}
