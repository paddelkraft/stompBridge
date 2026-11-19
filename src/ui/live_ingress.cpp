#include "live_ingress.h"

namespace stomp::ui {

void LiveIngress::connect(uint32_t epoch, SessionGeneration generation) {
  commands_.clear();
  epoch_ = epoch;
  generation_ = generation;
  lastRequestId_ = 0;
  connected_ = epoch != 0;
}

void LiveIngress::disconnect(uint32_t epoch) {
  if (!connected_ || epoch != epoch_) return;
  commands_.clear();
  connected_ = false;
}

void LiveIngress::pedalGeneration(SessionGeneration generation) {
  if (generation_ == generation) return;
  commands_.clear();
  generation_ = generation;
}

Result LiveIngress::enqueue(uint32_t requestId, uint32_t epoch,
                            SessionGeneration generation, CommandKind kind,
                            uint16_t target, bool enabled) {
  if (!connected_ || epoch != epoch_ || generation != generation_)
    return Result::Interrupted;
  if (kind != CommandKind::SelectPreset && kind != CommandKind::SetBlock &&
      kind != CommandKind::SetTuner) return Result::Unsupported;
  // Request IDs are strictly increasing during an epoch, making duplicate
  // detection bounded without retaining an unbounded set.
  if (requestId == 0 || requestId <= lastRequestId_) return Result::Invalid;
  if (commands_.size() == commands_.capacity()) return Result::QueueFull;
  CommandRequest request;
  request.requestId = requestId;
  request.uiEpoch = epoch;
  request.generation = generation;
  request.command = {kind, target, enabled, 0, generation};
  if (!commands_.push(request)) return Result::QueueFull;
  lastRequestId_ = requestId;
  return Result::Accepted;
}

bool LiveIngress::pop(CommandRequest &request) {
  while (commands_.pop(request)) {
    if (connected_ && request.uiEpoch == epoch_ &&
        request.generation == generation_) return true;
  }
  return false;
}

void ResultLedger::record(const RequestResult &result) {
  for (auto &entry : entries_) {
    if (entry.sequence >= oldestSequence_ && entry.sequence < nextSequence_ &&
        entry.result.uiEpoch == result.uiEpoch &&
        entry.result.requestId == result.requestId) {
      entry.result = result;
      return;
    }
  }
  const uint32_t sequence = nextSequence_++;
  entries_[sequence % kResultHistoryCapacity] = {sequence, result};
  if (nextSequence_ > kResultHistoryCapacity) {
    oldestSequence_ = nextSequence_ - kResultHistoryCapacity;
    ++lost_;
  }
}

bool ResultLedger::find(uint32_t epoch, uint32_t requestId,
                        RequestResult &result) const {
  for (const auto &entry : entries_) {
    if (entry.sequence >= oldestSequence_ && entry.sequence < nextSequence_ &&
        entry.result.uiEpoch == epoch && entry.result.requestId == requestId) {
      result = entry.result;
      return true;
    }
  }
  return false;
}

} // namespace stomp::ui
