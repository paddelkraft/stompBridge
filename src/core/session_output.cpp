#include "session_output.h"

namespace stomp {

void SessionOutput::begin(SessionGeneration generation) {
  stop();
  generation_ = generation;
  active_ = true;
}

void SessionOutput::stop() {
  liveQueue_.clear();
  readbackQueue_.clear();
  backgroundQueue_.clear();
  busy_ = false;
  active_ = false;
  current_ = {};
  pacingPending_ = false;
}

Result SessionOutput::enqueue(const OutputRequest &request) {
  if (!active_ || request.generation != generation_) return Result::NotReady;
  if (request.length == 0 || request.length > kMaxOutputMidi ||
      request.completionTimeoutMs == 0) return Result::Invalid;
  if (queued() >= kOutputQueueCapacity) return Result::QueueFull;
  const bool queued = request.background
                          ? backgroundQueue_.push(request)
                          : request.operation >= 0x80000000u
                                ? readbackQueue_.push(request)
                                : liveQueue_.push(request);
  if (!queued)
    return Result::QueueFull;
  observer_.outputEvent({request.operation, generation_, OperationPhase::Queued,
                         Result::Accepted});
  return Result::Accepted;
}

void SessionOutput::report(OperationPhase phase, Result result) {
  observer_.outputEvent({current_.operation, generation_, phase, result});
}

void SessionOutput::tick(uint32_t now) {
  if (!active_) return;
  if (busy_) {
    if (uint32_t(now - submittedAt_) >= current_.completionTimeoutMs) {
      report(OperationPhase::Failed, Result::TimedOut);
      busy_ = false;
      // A timed-out transfer might still complete. The session owner must
      // fault or replace the transport before another submission.
      active_ = false;
      liveQueue_.clear();
      readbackQueue_.clear();
      backgroundQueue_.clear();
    }
    return;
  }
  if (pacingPending_ && int32_t(now - nextAt_) < 0) return;
  pacingPending_ = false;
  if (!liveQueue_.pop(current_) && !readbackQueue_.pop(current_) &&
      !backgroundQueue_.pop(current_)) return;
  if (!port_.submit(current_.bytes, current_.length)) {
    report(OperationPhase::Failed, Result::TransportFailed);
    liveQueue_.clear();
    readbackQueue_.clear();
    backgroundQueue_.clear();
    active_ = false;
    current_ = {};
    return;
  }
  submittedAt_ = now;
  busy_ = true;
  report(OperationPhase::Submitted, Result::Accepted);
}

void SessionOutput::completed(SessionGeneration generation, bool success,
                              uint32_t now) {
  if (generation != generation_ || !active_ || !busy_) {
    ++staleCompletions_;
    return;
  }
  if (success) {
    report(OperationPhase::TransportComplete, Result::Accepted);
  } else {
    report(OperationPhase::Failed, Result::TransportFailed);
  }
  busy_ = false;
  nextAt_ = now + current_.pacingMs;
  pacingPending_ = current_.pacingMs != 0;
  current_ = {};
}

} // namespace stomp
