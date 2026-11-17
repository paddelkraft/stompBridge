#include "plugin.h"

#include <cstring>

namespace stomp::pocket {

namespace {
constexpr uint32_t kReadbackTimeoutMs = 2000;

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
  collector_.reset();
  generation_ = 0;
  stageSent_ = false;
  pendingCommandActive_ = false;
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

Result Plugin::start(SessionGeneration generation, uint32_t now) {
  stop();
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
  stage_ = Stage::Ready;
  sink_.synchronization(generation_, true, Result::Accepted);
}

void Plugin::onMidi(const MidiMessage &message, uint32_t now) {
  if (message.generation != generation_ || stage_ == Stage::Idle ||
      stage_ == Stage::Fault) return;
  Frame frame;
  if (!decodeFrame(message.bytes, message.length, frame)) return;
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
