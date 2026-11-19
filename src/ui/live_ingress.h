#pragma once

#include "core/bounded_queue.h"
#include "core/contracts.h"

namespace stomp::ui {

constexpr size_t kCommandQueueCapacity = 4;
constexpr size_t kResultHistoryCapacity = 8;

struct CommandRequest {
  uint32_t requestId = 0;
  uint32_t uiEpoch = 0;
  SessionGeneration generation = 0;
  Command command;
};

struct RequestResult {
  uint32_t requestId = 0;
  uint32_t uiEpoch = 0;
  SessionGeneration generation = 0;
  OperationId operation = 0;
  OperationPhase phase = OperationPhase::Queued;
  Result result = Result::Accepted;
  Result admission = Result::Accepted;
};

// Application-task-owned boundary. The platform callback copies validated
// requests to its task mailbox; this class is never called from a BLE callback.
class LiveIngress {
public:
  void connect(uint32_t epoch, SessionGeneration generation);
  void disconnect(uint32_t epoch);
  void pedalGeneration(SessionGeneration generation);
  Result enqueue(uint32_t requestId, uint32_t epoch,
                 SessionGeneration generation, CommandKind kind,
                 uint16_t target, bool enabled);
  bool pop(CommandRequest &request);
  uint32_t epoch() const { return epoch_; }
  SessionGeneration generation() const { return generation_; }
  size_t queued() const { return commands_.size(); }

private:
  BoundedQueue<CommandRequest, kCommandQueueCapacity> commands_;
  uint32_t epoch_ = 0;
  uint32_t lastRequestId_ = 0;
  SessionGeneration generation_ = 0;
  bool connected_ = false;
};

// Bounded correlated results; does not modify the read model or infer state.
class ResultLedger {
public:
  void record(const RequestResult &result);
  bool find(uint32_t epoch, uint32_t requestId, RequestResult &result) const;
  uint32_t oldestRetainedSequence() const { return oldestSequence_; }
  uint32_t nextSequence() const { return nextSequence_; }
  uint32_t lost() const { return lost_; }

private:
  struct Entry { uint32_t sequence = 0; RequestResult result; };
  Entry entries_[kResultHistoryCapacity] = {};
  uint32_t oldestSequence_ = 0;
  uint32_t nextSequence_ = 0;
  uint32_t lost_ = 0;
};

} // namespace stomp::ui
