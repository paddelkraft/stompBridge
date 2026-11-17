#include "application.h"

namespace stomp {

Application::Application(Clock &clock, MidiSendPort &port)
    : clock_(clock), manager_(devices::registry(), devices::registryCount()),
      output_(port, *this) {}

void Application::attach(const DeviceIdentity &identity) {
  output_.stop();
  storage_.destroy();
  plugin_ = nullptr;
  deferredCommandActive_ = false;
  togglePending_ = false;
  manager_.attach(identity);
  const auto generation = manager_.generation();
  ingress_.beginSession(generation);
  model_.setConnection(manager_.phase(), generation);
  const DeviceDescriptor *device = manager_.device();
  if (device == nullptr) return;
  output_.begin(generation);
  plugin_ = storage_.create(device->plugin, output_, *this);
  if (plugin_ == nullptr || !model_.setCapabilities(plugin_->capabilities()) ||
      plugin_->start(generation, clock_.nowMs()) != Result::Accepted) {
    if (manager_.phase() != ConnectionPhase::FaultBackoff)
      manager_.transportFault(generation, clock_.nowMs());
    model_.setConnection(manager_.phase(), generation);
    output_.stop();
  }
}

void Application::disconnect(SessionGeneration generation) {
  if (generation != manager_.generation()) {
    manager_.disconnected(generation);
    return;
  }
  output_.stop();
  storage_.destroy();
  plugin_ = nullptr;
  deferredCommandActive_ = false;
  togglePending_ = false;
  manager_.disconnected(generation);
  model_.setConnection(ConnectionPhase::Disconnected, generation);
}

bool Application::postMidi(SessionGeneration generation, const uint8_t *bytes,
                           size_t length) {
  return ingress_.midi(generation, bytes, length);
}

void Application::receiveLost() {
  const auto generation = manager_.generation();
  output_.stop();
  if (plugin_) plugin_->stop();
  togglePending_ = false;
  deferredCommandActive_ = false;
  if (manager_.phase() != ConnectionPhase::FaultBackoff)
    manager_.transportFault(generation, clock_.nowMs());
  model_.setConnection(manager_.phase(), generation);
  model_.setSync(SyncState::Failed, Result::TransportFailed);
}

void Application::pump() {
  ControlEvent control;
  while (ingress_.popControl(control)) {
    if (control.kind == ControlEventKind::Disconnected) {
      disconnect(control.generation);
      break;
    }
    if (control.kind == ControlEventKind::ReceiveLost) {
      if (control.generation != manager_.generation()) continue;
      receiveLost();
      break;
    }
    if (control.kind == ControlEventKind::SendComplete)
      output_.completed(control.generation, control.success, clock_.nowMs());
  }
  if (manager_.phase() == ConnectionPhase::FaultBackoff) manager_.tick(clock_.nowMs());
  model_.setConnection(manager_.phase(), manager_.generation());
  if (plugin_ == nullptr ||
      (manager_.phase() != ConnectionPhase::Synchronizing &&
       manager_.phase() != ConnectionPhase::Ready &&
       manager_.phase() != ConnectionPhase::Degraded)) return;
  MidiMessage message;
  while (ingress_.popMidi(message)) plugin_->onMidi(message, clock_.nowMs());
  plugin_->tick(clock_.nowMs());
  if (!deferredCommandActive_)
    deferredCommandActive_ = manager_.next(deferredCommand_);
  if (deferredCommandActive_) {
    const Result result = plugin_->command(deferredCommand_, clock_.nowMs());
    if (result != Result::QueueFull) {
      if (result != Result::Accepted)
        model_.setOperation(deferredCommand_.operation, OperationPhase::Failed,
                            result);
      deferredCommandActive_ = false;
    }
  }
  output_.tick(clock_.nowMs());
}

const BlockCapability *Application::block(uint16_t id) const {
  const auto &caps = model_.snapshot().capabilities;
  for (uint8_t i = 0; i < caps.blockCount; ++i)
    if (caps.blocks[i].id == id) return &caps.blocks[i];
  return nullptr;
}

bool Application::supported(CommandKind kind, uint16_t target) const {
  const auto &caps = model_.snapshot().capabilities;
  switch (kind) {
  case CommandKind::SelectPreset: return target < caps.presetCount;
  case CommandKind::SetBlock:
  case CommandKind::ToggleBlock: return block(target) != nullptr;
  case CommandKind::SetTuner:
  case CommandKind::ToggleTuner: return caps.tuner;
  case CommandKind::RequestReadback: return caps.presetReadback;
  }
  return false;
}

Admission Application::submit(CommandKind kind, uint16_t target, bool enabled) {
  if (manager_.device() == nullptr || plugin_ == nullptr ||
      !plugin_->readyForCommands()) return {Result::NotReady, 0};
  if (!supported(kind, target)) return {Result::Unsupported, 0};
  if (togglePending_ && (kind == CommandKind::ToggleBlock ||
                         kind == CommandKind::ToggleTuner))
    return {Result::QueueFull, 0};
  ++nextOperation_;
  if (nextOperation_ == 0 || nextOperation_ >= 0x80000000u)
    nextOperation_ = 1;
  Command command{kind, target, enabled, nextOperation_, manager_.generation()};
  if (kind == CommandKind::ToggleBlock) {
    const auto &state = model_.snapshot();
    for (uint8_t i = 0; i < state.capabilities.blockCount; ++i) {
      if (state.blocks[i].id != target) continue;
      const auto plan = planToggle(target, state.blocks[i].enabled,
                                   state.capabilities.blocks[i].readback,
                                   command.operation, command.generation);
      command = plan.command;
      if (plan.awaitingReadback) {
        togglePending_ = true;
        toggleTuner_ = false;
        toggleTarget_ = target;
        toggleOperation_ = command.operation;
      }
      break;
    }
  } else if (kind == CommandKind::ToggleTuner) {
    const auto value = model_.snapshot().tuner;
    if (value.certainty == Certainty::Unknown &&
        model_.snapshot().capabilities.tunerReadback) {
      command.kind = CommandKind::RequestReadback;
      togglePending_ = true;
      toggleTuner_ = true;
      toggleOperation_ = command.operation;
    } else {
      command.kind = CommandKind::SetTuner;
      command.enabled = value.certainty == Certainty::Unknown || !value.value;
    }
  }
  const Result result = manager_.admit(command);
  if (result != Result::Accepted) {
    togglePending_ = false;
    return {result, 0};
  }
  model_.setOperation(command.operation, OperationPhase::Queued, Result::Accepted);
  return {Result::Accepted, command.operation};
}

Admission Application::gesture(const GestureEvent &event) {
  if (event.kind != GestureKind::Press || event.switches == 0 ||
      (event.switches & (event.switches - 1u)) != 0)
    return {Result::Unsupported, 0};
  switch (event.switches) {
  case 1u << 0: bank(-1); return {Result::Accepted, 0};
  case 1u << 1: bank(1); return {Result::Accepted, 0};
  case 1u << 2: return submit(CommandKind::ToggleBlock, 2);
  case 1u << 3: return submit(CommandKind::ToggleBlock, 7);
  case 1u << 4: return submit(CommandKind::ToggleBlock, 8);
  case 1u << 5:
  case 1u << 6:
  case 1u << 7:
  case 1u << 8: {
    uint8_t slot = 0;
    while ((event.switches >> (slot + 5u)) != 1u) ++slot;
    return submit(CommandKind::SelectPreset,
                  uint16_t(model_.snapshot().localBank * 4u + slot));
  }
  case 1u << 9: return submit(CommandKind::ToggleTuner);
  default: return {Result::Unsupported, 0};
  }
}

void Application::bank(int8_t direction) {
  const int32_t next = int32_t(model_.snapshot().localBank) + direction;
  const uint16_t clamped = next < 0 ? 0 : next > 24 ? 24 : uint16_t(next);
  model_.setLocalBank(clamped);
}

void Application::outputEvent(const OutputEvent &event) {
  if (event.generation != manager_.generation()) return;
  if (plugin_) plugin_->onOutput(event, clock_.nowMs());
  if (event.operation != 0 && event.operation < 0x80000000u)
    model_.setOperation(event.operation, event.phase, event.result);
}

void Application::publish(const DeviceUpdate &update) {
  if (update.generation != manager_.generation()) return;
  if (update.kind == DeviceUpdateKind::Preset) {
    model_.setPreset(update.target, update.certainty);
    if (update.certainty == Certainty::Confirmed)
      model_.setLocalBank(update.target / 4);
  } else if (update.kind == DeviceUpdateKind::Block)
    model_.setBlock(update.target, update.enabled, update.certainty);
  else if (update.kind == DeviceUpdateKind::Tuner)
    model_.setTuner(update.enabled, update.certainty);
  else if (update.kind == DeviceUpdateKind::PresetName)
    model_.setPresetName(update.target, update.name);
  else if (update.kind == DeviceUpdateKind::NamesComplete)
    model_.namesComplete();
  else if (update.kind == DeviceUpdateKind::BlockModel)
    model_.setBlockModel(update.target, update.modelId, update.name,
                         update.certainty);
}

void Application::synchronizationStarted(SessionGeneration generation) {
  if (generation != manager_.generation()) return;
  manager_.resynchronize(generation);
  model_.setSync(SyncState::Pending);
  model_.setConnection(manager_.phase(), generation);
}

void Application::synchronization(SessionGeneration generation, bool verified,
                                  Result outcome) {
  if (generation != manager_.generation()) return;
  manager_.synchronized(generation, verified, clock_.nowMs());
  model_.setSync(verified ? SyncState::Confirmed
                          : outcome == Result::Interrupted ? SyncState::Interrupted
                                                           : SyncState::Failed,
                 outcome);
  model_.setConnection(manager_.phase(), generation);
  if (!verified) {
    if (togglePending_)
      model_.setOperation(toggleOperation_, OperationPhase::Failed, outcome);
    togglePending_ = false;
    return;
  }
  if (!togglePending_) return;
  const auto &snapshot = model_.snapshot();
  BooleanValue observed;
  if (toggleTuner_) {
    observed = snapshot.tuner;
  } else {
    for (uint8_t i = 0; i < snapshot.capabilities.blockCount; ++i)
      if (snapshot.blocks[i].id == toggleTarget_) observed = snapshot.blocks[i].enabled;
  }
  if (observed.certainty != Certainty::Confirmed) {
    model_.setOperation(toggleOperation_, OperationPhase::Failed, Result::TimedOut);
    togglePending_ = false;
    return;
  }
  const Command command{toggleTuner_ ? CommandKind::SetTuner : CommandKind::SetBlock,
                        toggleTarget_, !observed.value, toggleOperation_, generation};
  togglePending_ = false;
  if (manager_.admit(command) != Result::Accepted)
    model_.setOperation(command.operation, OperationPhase::Failed, Result::QueueFull);
}

} // namespace stomp
