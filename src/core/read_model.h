#pragma once

#include "contracts.h"

namespace stomp {

struct BlockValue {
  BlockId id = 0;
  BooleanValue enabled;
  uint32_t modelId = 0;
  char modelName[kMaxBlockLabel] = {};
  Certainty modelCertainty = Certainty::Unknown;
};

struct ReadSnapshot {
  uint16_t schema = 2;
  uint32_t revision = 1;
  SessionGeneration generation = 0;
  ConnectionPhase connection = ConnectionPhase::Disconnected;
  SyncState sync = SyncState::Idle;
  Result syncResult = Result::Accepted;
  uint32_t syncRevision = 0;
  DeviceCapabilities capabilities;
  uint16_t localBank = 0;
  PresetValue preset;
  BooleanValue tuner;
  BlockValue blocks[kMaxBlocks] = {};
  char presetNames[kMaxPresets][kMaxPresetName] = {};
  bool presetNamesComplete = false;
  uint32_t namesRevision = 0;
  Result lastResult = Result::Accepted;
  OperationPhase lastPhase = OperationPhase::Queued;
  OperationId lastOperation = 0;
};

class ReadModel {
public:
  const ReadSnapshot &snapshot() const { return state_; }
  void setConnection(ConnectionPhase phase, SessionGeneration generation);
  void setSync(SyncState state, Result result = Result::Accepted);
  bool setCapabilities(const DeviceCapabilities &capabilities);
  void setLocalBank(uint16_t bank);
  void setPreset(uint16_t index, Certainty certainty);
  bool setBlock(BlockId id, bool enabled, Certainty certainty);
  bool setBlockModel(BlockId id, uint32_t modelId, const char *name,
                     Certainty certainty);
  bool setPresetName(uint16_t index, const char *name);
  void namesComplete();
  void setTuner(bool enabled, Certainty certainty);
  void setOperation(OperationId id, OperationPhase phase, Result result);

private:
  void invalidateObserved();
  ReadSnapshot state_;
};

} // namespace stomp
