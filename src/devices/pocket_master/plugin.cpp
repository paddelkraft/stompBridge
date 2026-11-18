#include "plugin.h"

#include <cstring>

namespace stomp::pocket {

namespace {
constexpr uint32_t kReadbackTimeoutMs = 2000;

void label(BlockCapability &block, uint16_t id, const char *name) {
  block.id = id;
  block.readback = true;
  block.modelReadback = true;
  size_t i = 0;
  while (i + 1 < kMaxBlockLabel && name[i]) {
    block.label[i] = name[i];
    ++i;
  }
  block.label[i] = '\0';
}

struct ModelName { uint32_t id; const char *name; };
constexpr ModelName kDriveModels[] = {
    {0x03000000, "Scream"}, {0x03000002, "Butter OD"},
    {0x0300002a, "JP Dist"}, {0x03000030, "Shark"},
    {0x0300002b, "Dark Mouse"}, {0x03000022, "Grey Fuzz"},
    {0x03000024, "Red Fuzz"}, {0x03000040, "Bass Drive"},
};
constexpr ModelName kDelayModels[] = {
    {0x0b000000, "Pure"}, {0x0b000005, "Slap"},
    {0x0b00000d, "Warm"}, {0x0b000002, "Mag"},
    {0x0b00000b, "Tube"}, {0x0b000013, "Reverse"},
    {0x0b000014, "Analog"}, {0x0b000006, "Sweep"},
    {0x0b000004, "Ping Pong"},
};
constexpr ModelName kReverbModels[] = {
    {0x0c00000b, "Air"}, {0x0c000000, "Room"},
    {0x0c000001, "Hall"}, {0x0c000002, "Church"},
    {0x0c000010, "Plate 1"}, {0x0c00000f, "Plate 2"},
    {0x0c000004, "Spring"}, {0x0c000006, "Light"},
    {0x0c000007, "Ocean"}, {0x0c000015, "Dream"},
};
template <size_t N>
const char *findName(const ModelName (&models)[N], uint32_t id) {
  for (const auto &model : models) if (model.id == id) return model.name;
  return nullptr;
}
const char *modelName(uint16_t block, uint32_t id) {
  switch (block) {
  case 2: return findName(kDriveModels, id);
  case 7: return findName(kDelayModels, id);
  case 8: return findName(kReverbModels, id);
  default: return nullptr;
  }
}
} // namespace

Plugin::Plugin(SessionOutput &output, DeviceUpdateSink &sink)
    : output_(output), sink_(sink) {
  capabilities_.presetCount = 100;
  capabilities_.presetReadback = true;
  capabilities_.presetNameReadback = true;
  capabilities_.presetSlotLayout = PresetSlotLayout::PocketPresetAndEffect;
  capabilities_.blockCount = 3;
  capabilities_.tuner = true;
  capabilities_.tunerReadback = false;
  label(capabilities_.blocks[0], 2, "Drive");
  label(capabilities_.blocks[1], 7, "Delay");
  label(capabilities_.blocks[2], 8, "Reverb");
}

void Plugin::stop() {
  stage_ = Stage::Idle;
  collector_.reset();
  generation_ = 0;
  stageSent_ = false;
  pendingCommandActive_ = false;
  names_.reset();
  namesRequested_ = false;
  nameAttempts_ = 0;
}

void Plugin::fail(Result reason) {
  if (stage_ == Stage::Fault || stage_ == Stage::Idle) return;
  stage_ = Stage::Fault;
  collector_.reset();
  sink_.synchronization(generation_, false, reason);
}

Result Plugin::requestPreset(uint32_t now) {
  OutputRequest request;
  request.operation = ++internalOperation_;
  request.generation = generation_;
  request.length = static_cast<uint16_t>(currentPresetRequest(request.bytes,
                                                              sizeof(request.bytes)));
  const Result result = output_.enqueue(request);
  if (result == Result::Accepted) {
    stageStartedAt_ = now;
    stageSent_ = false;
  }
  return result;
}

Result Plugin::requestLive(uint32_t now) {
  OutputRequest request;
  request.operation = ++internalOperation_;
  request.generation = generation_;
  request.length = static_cast<uint16_t>(liveStateRequest(request.bytes,
                                                         sizeof(request.bytes)));
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
      nameDirectoryRequest(request.bytes, sizeof(request.bytes)));
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
  collector_.reset();
  stageSent_ = false;
  pendingCommandActive_ = false;
  generation_ = generation;
  stage_ = Stage::FirstPreset;
  sink_.synchronizationStarted(generation_);
  const Result result = requestPreset(now);
  if (result != Result::Accepted) fail(result);
  return result;
}

void Plugin::publishConfirmed(uint8_t preset, const LiveBlocks &blocks) {
  sink_.publish({DeviceUpdateKind::Preset, generation_, preset, false,
                 Certainty::Confirmed});
  sink_.publish({DeviceUpdateKind::Block, generation_, 2, blocks.drive,
                 Certainty::Confirmed});
  sink_.publish({DeviceUpdateKind::Block, generation_, 7, blocks.delay,
                 Certainty::Confirmed});
  sink_.publish({DeviceUpdateKind::Block, generation_, 8, blocks.reverb,
                 Certainty::Confirmed});
  if (blocks.modelIdsValid) {
    const uint16_t ids[] = {2, 7, 8};
    const uint32_t models[] = {blocks.driveModelId, blocks.delayModelId,
                               blocks.reverbModelId};
    for (size_t i = 0; i < 3; ++i) {
      DeviceUpdate update{DeviceUpdateKind::BlockModel, generation_, ids[i],
                          false, Certainty::Confirmed};
      update.modelId = models[i];
      const char *name = modelName(ids[i], models[i]);
      if (name != nullptr) {
        size_t n = 0;
        while (n + 1 < sizeof(update.name) && name[n] != '\0') {
          update.name[n] = name[n];
          ++n;
        }
      }
      sink_.publish(update);
    }
  }
  stage_ = Stage::Ready;
  sink_.synchronization(generation_, true, Result::Accepted);
}

void Plugin::consumeNames(const Frame &frame) {
  if (!namesRequested_ || !names_.accept(frame)) return;
  for (uint8_t i = 0; i < 100; ++i) {
    DeviceUpdate update{DeviceUpdateKind::PresetName, generation_, i,
                        false, Certainty::Confirmed};
    const char *source = names_.name(i);
    size_t n = 0;
    while (n < 16 && source[n] != '\0') {
      update.name[n] = source[n];
      ++n;
    }
    while (n > 0 && update.name[n - 1] == ' ') update.name[--n] = '\0';
    sink_.publish(update);
  }
  sink_.publish({DeviceUpdateKind::NamesComplete, generation_});
}

void Plugin::onMidi(const MidiMessage &message, uint32_t now) {
  if (message.generation != generation_ || stage_ == Stage::Idle ||
      stage_ == Stage::Fault) return;
  Frame frame;
  if (!decodeFrame(message.bytes, message.length, frame)) return;
  consumeNames(frame);
  if (stage_ == Stage::Ready) {
    if (pendingCommandActive_) return;
    uint8_t module = 0;
    bool enabled = false;
    if (decodeModuleFeedback(frame.payload, frame.length, module, enabled) &&
        (module == 2 || module == 7 || module == 8))
      sink_.publish({DeviceUpdateKind::Block, generation_, module, enabled,
                     Certainty::Confirmed});
    return;
  }
  if (!stageSent_) return;
  if (stage_ == Stage::FirstPreset) {
    uint8_t preset = 0;
    if (!decodeCurrentPreset(frame.payload, frame.length, preset)) return;
    firstPreset_ = preset;
    collector_.reset();
    stage_ = Stage::Live;
    const Result result = requestLive(now);
    if (result != Result::Accepted) fail(result);
    return;
  }
  if (stage_ == Stage::Live) {
    if (!collector_.accept(frame)) return;
    if (!decodeLiveBlocks(collector_.logical(), collector_.length(),
                          liveBlocks_)) {
      fail(Result::Invalid);
      return;
    }
    stage_ = Stage::SecondPreset;
    const Result result = requestPreset(now);
    if (result != Result::Accepted) fail(result);
    return;
  }
  if (stage_ == Stage::SecondPreset) {
    uint8_t second = 0;
    if (!decodeCurrentPreset(frame.payload, frame.length, second)) return;
    if (second != firstPreset_) {
      fail(Result::Invalid);
      return;
    }
    publishConfirmed(second, liveBlocks_);
    if (!namesRequested_ && !names_.complete()) requestNames(now);
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
      if (completed.kind == CommandKind::SelectPreset) {
        sink_.publish({DeviceUpdateKind::Preset, generation_, completed.target,
                       false, Certainty::Assumed});
      } else if (completed.kind == CommandKind::SetBlock) {
        sink_.publish({DeviceUpdateKind::Block, generation_, completed.target,
                       completed.enabled, Certainty::Assumed});
      } else if (completed.kind == CommandKind::SetTuner) {
        sink_.publish({DeviceUpdateKind::Tuner, generation_, 0,
                       completed.enabled, Certainty::Assumed});
      }
      if (completed.kind != CommandKind::SetTuner)
        start(generation_, now);
    }
    return;
  }
  if (event.operation != internalOperation_) return;
  if (event.phase == OperationPhase::Submitted) stageSent_ = true;
  if (event.phase == OperationPhase::Failed && stage_ != Stage::Ready)
    fail(event.result);
}

void Plugin::tick(uint32_t now) {
  if (stage_ == Stage::Ready && !names_.complete() && nameAttempts_ < 4 &&
      (!namesRequested_ || uint32_t(now - lastNameRequestAt_) >= 3000)) {
    names_.reset();
    requestNames(now);
  }
  if (stage_ != Stage::Idle && stage_ != Stage::Ready &&
      stage_ != Stage::Fault &&
      uint32_t(now - stageStartedAt_) >= kReadbackTimeoutMs) fail();
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
  uint8_t control = 0;
  uint8_t value = 0;
  if (command.kind == CommandKind::SelectPreset && command.target < 100) {
    control = 1;
    value = uint8_t(command.target + 1);
  } else if (command.kind == CommandKind::SetBlock) {
    if (command.target == 2) control = 45;
    else if (command.target == 7) control = 50;
    else if (command.target == 8) control = 51;
    else return Result::Unsupported;
    value = command.enabled ? 127 : 0;
  } else if (command.kind == CommandKind::SetTuner) {
    control = 58;
    value = command.enabled ? 127 : 0;
  } else {
    return Result::Unsupported;
  }
  OutputRequest request;
  request.operation = command.operation;
  request.generation = generation_;
  request.length = 3;
  request.pacingMs = 150;
  request.bytes[0] = 0xb0;
  request.bytes[1] = control;
  request.bytes[2] = value;
  const Result result = output_.enqueue(request);
  if (result == Result::Accepted) {
    pendingCommand_ = command;
    pendingCommandActive_ = true;
  }
  return result;
}

} // namespace stomp::pocket
