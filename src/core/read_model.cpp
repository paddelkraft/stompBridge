#include "read_model.h"

#include <cstring>

namespace stomp {

namespace {
bool sameCapabilities(const DeviceCapabilities &a, const DeviceCapabilities &b) {
  if (a.presetCount != b.presetCount || a.blockCount != b.blockCount ||
      a.presetReadback != b.presetReadback || a.tuner != b.tuner ||
      a.tunerReadback != b.tunerReadback) return false;
  for (uint8_t i = 0; i < a.blockCount; ++i) {
    if (a.blocks[i].id != b.blocks[i].id ||
        a.blocks[i].readback != b.blocks[i].readback) return false;
    for (size_t c = 0; c < kMaxBlockLabel; ++c) {
      if (a.blocks[i].label[c] != b.blocks[i].label[c]) return false;
    }
  }
  return true;
}
} // namespace

void ReadModel::invalidateObserved() {
  state_.preset.certainty = Certainty::Unknown;
  state_.tuner.certainty = Certainty::Unknown;
  for (auto &block : state_.blocks) {
    block.enabled.certainty = Certainty::Unknown;
    block.modelCertainty = Certainty::Unknown;
  }
}

void ReadModel::setConnection(ConnectionPhase phase,
                              SessionGeneration generation) {
  if (state_.connection == phase && state_.generation == generation) return;
  if (generation != state_.generation) {
    state_.capabilities = {};
    for (auto &block : state_.blocks) block = {};
    for (auto &name : state_.presetNames) name[0] = '\0';
    state_.presetNamesComplete = false;
    ++state_.namesRevision;
    state_.sync = SyncState::Idle;
    state_.syncResult = Result::Accepted;
    ++state_.syncRevision;
  }
  if (generation != state_.generation || phase == ConnectionPhase::Disconnected ||
      phase == ConnectionPhase::FaultBackoff ||
      phase == ConnectionPhase::Degraded) {
    invalidateObserved();
  }
  state_.connection = phase;
  state_.generation = generation;
  if (phase == ConnectionPhase::Disconnected ||
      phase == ConnectionPhase::FaultBackoff) {
    state_.sync = SyncState::Idle;
    state_.syncResult = Result::Accepted;
    ++state_.syncRevision;
  }
  ++state_.revision;
}

void ReadModel::setSync(SyncState state, Result result) {
  state_.sync = state;
  state_.syncResult = result;
  ++state_.syncRevision;
  ++state_.revision;
}

bool ReadModel::setCapabilities(const DeviceCapabilities &capabilities) {
  if (capabilities.blockCount > kMaxBlocks ||
      capabilities.presetCount > kMaxPresets) return false;
  for (uint8_t i = 0; i < capabilities.blockCount; ++i) {
    bool terminated = false;
    for (char c : capabilities.blocks[i].label) {
      if (c == '\0') terminated = true;
    }
    if (!terminated) return false;
    for (uint8_t j = 0; j < i; ++j) {
      if (capabilities.blocks[i].id == capabilities.blocks[j].id) return false;
    }
  }
  if (sameCapabilities(state_.capabilities, capabilities)) return true;
  state_.capabilities = capabilities;
  for (uint8_t i = 0; i < kMaxBlocks; ++i) {
    state_.blocks[i] = {};
    state_.blocks[i].id = i < capabilities.blockCount ? capabilities.blocks[i].id : 0;
  }
  state_.preset = {};
  state_.tuner = {};
  for (auto &name : state_.presetNames) name[0] = '\0';
  state_.presetNamesComplete = false;
  ++state_.namesRevision;
  ++state_.revision;
  return true;
}

void ReadModel::setLocalBank(uint16_t bank) {
  if (state_.localBank != bank) {
    state_.localBank = bank;
    ++state_.revision;
  }
}

void ReadModel::setPreset(uint16_t index, Certainty certainty) {
  if (state_.preset.index != index || state_.preset.certainty != certainty) {
    if (state_.preset.index != index)
      for (auto &block : state_.blocks)
        block.modelCertainty = Certainty::Unknown;
    state_.preset = {index, certainty};
    ++state_.revision;
  }
}

bool ReadModel::setBlock(BlockId id, bool enabled, Certainty certainty) {
  for (uint8_t i = 0; i < state_.capabilities.blockCount; ++i) {
    auto &block = state_.blocks[i];
    if (block.id != id) continue;
    if (block.enabled.value != enabled || block.enabled.certainty != certainty) {
      block.enabled = {enabled, certainty};
      ++state_.revision;
    }
    return true;
  }
  return false;
}

bool ReadModel::setBlockModel(BlockId id, uint32_t modelId, const char *name,
                             Certainty certainty) {
  for (uint8_t i = 0; i < state_.capabilities.blockCount; ++i) {
    auto &block = state_.blocks[i];
    if (block.id != id) continue;
    if (block.modelId == modelId && block.modelCertainty == certainty &&
        strncmp(block.modelName, name ? name : "", kMaxBlockLabel) == 0)
      return true;
    block.modelId = modelId;
    block.modelCertainty = certainty;
    size_t n = 0;
    if (name != nullptr)
      while (n + 1 < kMaxBlockLabel && name[n] != '\0') {
        block.modelName[n] = name[n];
        ++n;
      }
    block.modelName[n] = '\0';
    ++state_.revision;
    ++state_.namesRevision;
    return true;
  }
  return false;
}

bool ReadModel::setPresetName(uint16_t index, const char *name) {
  if (index >= state_.capabilities.presetCount || name == nullptr) return false;
  char copy[kMaxPresetName] = {};
  size_t n = 0;
  while (n + 1 < kMaxPresetName && name[n] != '\0') {
    copy[n] = name[n];
    ++n;
  }
  if (memcmp(state_.presetNames[index], copy, sizeof(copy)) == 0) return true;
  memcpy(state_.presetNames[index], copy, sizeof(copy));
  ++state_.revision;
  ++state_.namesRevision;
  return true;
}

void ReadModel::namesComplete() {
  if (state_.presetNamesComplete) return;
  state_.presetNamesComplete = true;
  ++state_.revision;
  ++state_.namesRevision;
}

void ReadModel::setTuner(bool enabled, Certainty certainty) {
  if (state_.tuner.value != enabled || state_.tuner.certainty != certainty) {
    state_.tuner = {enabled, certainty};
    ++state_.revision;
  }
}

void ReadModel::setOperation(OperationId id, OperationPhase phase, Result result) {
  if (state_.lastOperation != id || state_.lastPhase != phase ||
      state_.lastResult != result) {
    state_.lastOperation = id;
    state_.lastPhase = phase;
    state_.lastResult = result;
    ++state_.revision;
  }
}

} // namespace stomp
