#include "plugin.h"

#include "gp5_protocol.h"
#include "gp5_models.h"

#include <cstring>

namespace stomp::gp5 {

namespace {
constexpr uint32_t kSyncTimeoutMs = 5000;

void label(BlockCapability &block, uint16_t id, const char *name) {
  block.id = id;
  block.readback = true;
  size_t i = 0;
  while (i + 1 < kMaxBlockLabel && name[i]) {
    block.label[i] = name[i];
    ++i;
  }
  block.label[i] = '\0';
}
} // namespace

Plugin::Plugin(SessionOutput &output, DeviceUpdateSink &sink)
    : output_(output), sink_(sink) {
  capabilities_.presetCount = 100;
  capabilities_.presetReadback = true;
  capabilities_.blockCount = 3;
  capabilities_.tuner = true;
  capabilities_.tunerReadback = false;
  label(capabilities_.blocks[0], 2, "Drive");
  label(capabilities_.blocks[1], 7, "Delay");
  label(capabilities_.blocks[2], 8, "Reverb");
}

void Plugin::stop() {
  stage_ = Stage::Idle;
  generation_ = 0;
  stageSent_ = false;
  pendingCommandActive_ = false;
  namesRequested_ = false;
  namesComplete_ = false;
  nameAttempts_ = 0;
  lastNameRequestAt_ = 0;
  memset(nameFragments_, 0, sizeof(nameFragments_));
  nameFragmentCount_ = 0;
  nextModelFragment_ = 0;
  modelReady_ = false;
  modelPublished_ = false;
}

void Plugin::fail(Result reason) {
  if (stage_ == Stage::Idle || stage_ == Stage::Fault) return;
  stage_ = Stage::Fault;
  sink_.synchronization(generation_, false, reason);
}

Result Plugin::requestPreset(uint32_t now) {
  OutputRequest request;
  request.operation = ++internalOperation_;
  request.generation = generation_;
  request.length = static_cast<uint16_t>(
      buildGp5CurrentPresetRequest(request.bytes, sizeof(request.bytes)));
  request.pacingMs = 100;
  const Result result = output_.enqueue(request);
  if (result == Result::Accepted) {
    stageStartedAt_ = now;
    stageSent_ = false;
  }
  return result;
}

Result Plugin::requestPatch(uint32_t now) {
  OutputRequest request;
  request.operation = ++internalOperation_;
  request.generation = generation_;
  request.length = static_cast<uint16_t>(
      buildGp5PatchStateRequest(request.bytes, sizeof(request.bytes)));
  request.pacingMs = 100;
  const Result result = output_.enqueue(request);
  if (result == Result::Accepted) {
    stageStartedAt_ = now;
    stageSent_ = false;
  }
  return result;
}

Result Plugin::requestNames(uint32_t now) {
  OutputRequest request;
  request.operation = ++internalOperation_;
  request.generation = generation_;
  request.length = static_cast<uint16_t>(
      buildGp5SyncStartRequest(request.bytes, sizeof(request.bytes)));
  request.pacingMs = 100;
  request.background = true;
  const Result result = output_.enqueue(request);
  if (result == Result::Accepted) {
    namesRequested_ = true;
    ++nameAttempts_;
    lastNameRequestAt_ = now;
  }
  return result;
}

Result Plugin::start(SessionGeneration generation, uint32_t now) {
  if (generation != generation_) stop();
  stageSent_ = false;
  pendingCommandActive_ = false;
  nextModelFragment_ = 0;
  modelReady_ = false;
  modelPublished_ = false;
  generation_ = generation;
  stage_ = Stage::FirstPreset;
  sink_.synchronizationStarted(generation_);
  const Result result = requestPreset(now);
  if (result != Result::Accepted) fail(result);
  return result;
}

void Plugin::publishConfirmed(uint8_t preset, const Blocks &blocks,
                              uint32_t now) {
  sink_.publish({DeviceUpdateKind::Preset, generation_, preset, false,
                 Certainty::Confirmed});
  sink_.publish({DeviceUpdateKind::Block, generation_, 2, blocks.drive,
                 Certainty::Confirmed});
  sink_.publish({DeviceUpdateKind::Block, generation_, 7, blocks.delay,
                 Certainty::Confirmed});
  sink_.publish({DeviceUpdateKind::Block, generation_, 8, blocks.reverb,
                 Certainty::Confirmed});
  stage_ = Stage::Ready;
  sink_.synchronization(generation_, true, Result::Accepted);
  publishModels();
  if (!namesRequested_ && !namesComplete_) requestNames(now);
}

void Plugin::collectModelFragment(const MidiMessage &message) {
  if (modelReady_ || nextModelFragment_ >= 7) return;
  uint8_t command = 0;
  uint8_t fragment = 0;
  uint8_t payload[19] = {};
  size_t length = 0;
  if (!decodeGp5Fragment(message.bytes, message.length, command, fragment,
                         payload, sizeof(payload), length) ||
      command != 0x19 || length != 19) return;
  if (fragment != nextModelFragment_) {
    if (fragment != 0) return;
    nextModelFragment_ = 0;
  }
  memcpy(modelLogical_ + size_t(nextModelFragment_) * 19, payload, 19);
  ++nextModelFragment_;
  if (nextModelFragment_ != 7) return;
  Gp5PatchDetails details;
  if (!decodeGp5PatchDetails(modelLogical_, sizeof(modelLogical_), details))
    return;
  modelIds_[0] = details.driveModelId;
  modelIds_[1] = details.delayModelId;
  modelIds_[2] = details.reverbModelId;
  modelReady_ = true;
  publishModels();
}

void Plugin::publishModels() {
  if (!modelReady_ || modelPublished_ || stage_ != Stage::Ready) return;
  constexpr uint16_t ids[] = {2, 7, 8};
  for (uint8_t i = 0; i < 3; ++i) {
    DeviceUpdate update{DeviceUpdateKind::BlockModel, generation_, ids[i],
                        false, Certainty::Confirmed};
    update.modelId = modelIds_[i];
    const char *name = gp5EffectModelName(uint8_t(ids[i]), modelIds_[i]);
    if (name != nullptr) {
      size_t n = 0;
      while (n + 1 < sizeof(update.name) && name[n] != '\0') {
        update.name[n] = name[n];
        ++n;
      }
    }
    sink_.publish(update);
  }
  modelPublished_ = true;
}

void Plugin::consumeNames(const MidiMessage &message) {
  if (!namesRequested_ || namesComplete_) return;
  uint8_t command = 0;
  uint8_t fragment = 0;
  uint8_t payload[19] = {};
  size_t length = 0;
  if (!decodeGp5Fragment(message.bytes, message.length, command, fragment,
                         payload, sizeof(payload), length) ||
      command != 0x6a || fragment >= 106 ||
      length != (fragment == 105 ? 7u : 19u) ||
      nameFragments_[fragment]) return;
  memcpy(nameLogical_ + size_t(fragment) * 19, payload, length);
  nameFragments_[fragment] = true;
  ++nameFragmentCount_;
  if (nameFragmentCount_ == 106) {
    if (nameLogical_[0] != 0x12 || nameLogical_[1] != 0x40) return;
    for (uint8_t i = 0; i < 100; ++i) {
      const uint8_t *record = nameLogical_ + 2 + size_t(i) * 20;
      if (record[0] != i || record[1] != 0 || record[2] != 0 ||
          record[3] != 0) return;
    }
    for (uint8_t i = 0; i < 100; ++i) {
      DeviceUpdate update{DeviceUpdateKind::PresetName, generation_, i,
                          false, Certainty::Confirmed};
      const uint8_t *record = nameLogical_ + 2 + size_t(i) * 20;
      size_t n = 0;
      while (n < 10 && record[4 + n] >= 0x20 &&
             record[4 + n] <= 0x7e) {
        update.name[n] = static_cast<char>(record[4 + n]);
        ++n;
      }
      while (n > 0 && update.name[n - 1] == ' ') update.name[--n] = '\0';
      sink_.publish(update);
    }
    namesComplete_ = true;
    sink_.publish({DeviceUpdateKind::NamesComplete, generation_});
  }
}

void Plugin::onMidi(const MidiMessage &message, uint32_t now) {
  if (message.generation != generation_ || stage_ == Stage::Idle ||
      stage_ == Stage::Fault) return;
  if (stage_ == Stage::PatchState || stage_ == Stage::SecondPreset ||
      stage_ == Stage::Ready)
    collectModelFragment(message);
  consumeNames(message);
  if (stage_ == Stage::Ready) return;
  if (!stageSent_) return;
  if (stage_ == Stage::FirstPreset) {
    uint8_t index = 0;
    if (!currentPreset(message.bytes, message.length, index)) return;
    firstPreset_ = index;
    stage_ = Stage::PatchState;
    const Result result = requestPatch(now);
    if (result != Result::Accepted) fail(result);
    return;
  }
  if (stage_ == Stage::PatchState) {
    if (!patchStateFragment(message.bytes, message.length, blocks_)) return;
    stage_ = Stage::SecondPreset;
    const Result result = requestPreset(now);
    if (result != Result::Accepted) fail(result);
    return;
  }
  if (stage_ == Stage::SecondPreset) {
    uint8_t index = 0;
    if (!currentPreset(message.bytes, message.length, index)) return;
    if (index != firstPreset_) {
      fail(Result::Invalid);
      return;
    }
    publishConfirmed(index, blocks_, now);
  }
}

void Plugin::onOutput(const OutputEvent &event, uint32_t now) {
  if (event.generation != generation_) return;
  if (pendingCommandActive_ && event.operation == pendingCommand_.operation) {
    if (event.phase == OperationPhase::Failed) {
      pendingCommandActive_ = false;
      fail(event.result);
    } else if (event.phase == OperationPhase::TransportComplete) {
      const Command completed = pendingCommand_;
      pendingCommandActive_ = false;
      if (completed.kind == CommandKind::SelectPreset)
        sink_.publish({DeviceUpdateKind::Preset, generation_, completed.target,
                       false, Certainty::Assumed});
      else if (completed.kind == CommandKind::SetBlock)
        sink_.publish({DeviceUpdateKind::Block, generation_, completed.target,
                       completed.enabled, Certainty::Assumed});
      else if (completed.kind == CommandKind::SetTuner)
        sink_.publish({DeviceUpdateKind::Tuner, generation_, 0,
                       completed.enabled, Certainty::Assumed});
      if (completed.kind != CommandKind::SetTuner) start(generation_, now);
    }
    return;
  }
  if (event.operation != internalOperation_) return;
  if (event.phase == OperationPhase::Submitted) stageSent_ = true;
  if (event.phase == OperationPhase::Failed && stage_ != Stage::Ready)
    fail(event.result);
}

void Plugin::tick(uint32_t now) {
  if (stage_ == Stage::Ready && !namesComplete_ && nameAttempts_ < 4 &&
      (!namesRequested_ || uint32_t(now - lastNameRequestAt_) >= 3000))
    requestNames(now);
  if (stage_ != Stage::Idle && stage_ != Stage::Ready &&
      stage_ != Stage::Fault && uint32_t(now - stageStartedAt_) >= kSyncTimeoutMs)
    fail();
}

Result Plugin::command(const Command &command, uint32_t now) {
  if (stage_ == Stage::Idle || command.generation != generation_)
    return Result::NotReady;
  if (command.operation == 0 || command.operation >= 0x80000000u)
    return Result::Invalid;
  if (pendingCommandActive_) return Result::QueueFull;
  if (command.kind == CommandKind::RequestReadback)
    return start(generation_, now);
  if (stage_ != Stage::Ready && stage_ != Stage::Fault)
    fail(Result::Interrupted);
  OutputRequest request;
  request.operation = command.operation;
  request.generation = generation_;
  request.pacingMs = 350;
  if (command.kind == CommandKind::SelectPreset) {
    if (command.target >= capabilities_.presetCount) return Result::Unsupported;
    request.length = static_cast<uint16_t>(buildGp5PresetChange(
        uint8_t(command.target), request.bytes, sizeof(request.bytes)));
    if (request.length == 0) return Result::Unsupported;
  } else {
    uint8_t control = 0;
    if (command.kind == CommandKind::SetBlock) {
      if (command.target == 2) control = 50;
      else if (command.target == 7) control = 56;
      else if (command.target == 8) control = 57;
      else return Result::Unsupported;
    } else if (command.kind == CommandKind::SetTuner) {
      control = 58;
    } else {
      return Result::Unsupported;
    }
    request.length = 3;
    request.bytes[0] = 0xb0;
    request.bytes[1] = control;
    request.bytes[2] = command.enabled ? 127 : 0;
  }
  const Result result = output_.enqueue(request);
  if (result == Result::Accepted) {
    pendingCommand_ = command;
    pendingCommandActive_ = true;
  }
  return result;
}

} // namespace stomp::gp5
