#pragma once

#include "bounded_queue.h"
#include "contracts.h"

namespace stomp {

constexpr size_t kMaxOutputMidi = 128;
constexpr size_t kOutputQueueCapacity = 8;

struct OutputRequest {
  OperationId operation = 0;
  SessionGeneration generation = 0;
  uint16_t length = 0;
  uint16_t pacingMs = 0;
  uint16_t completionTimeoutMs = 1000;
  bool background = false;
  uint8_t bytes[kMaxOutputMidi] = {};
};

struct OutputEvent {
  OperationId operation = 0;
  SessionGeneration generation = 0;
  OperationPhase phase = OperationPhase::Queued;
  Result result = Result::Accepted;
};

class MidiSendPort {
public:
  virtual ~MidiSendPort() = default;
  // Copy the bytes before returning and retain that copy until completion.
  // The caller may reset its queue immediately after a disconnect.
  virtual bool submit(const uint8_t *bytes, size_t length) = 0;
};

class OutputObserver {
public:
  virtual ~OutputObserver() = default;
  virtual void outputEvent(const OutputEvent &event) = 0;
};

class SessionOutput {
public:
  SessionOutput(MidiSendPort &port, OutputObserver &observer)
      : port_(port), observer_(observer) {}
  void begin(SessionGeneration generation);
  void stop();
  Result enqueue(const OutputRequest &request);
  void tick(uint32_t now);
  void completed(SessionGeneration generation, bool success, uint32_t now);
  size_t queued() const {
    return liveQueue_.size() + readbackQueue_.size() + backgroundQueue_.size();
  }
  bool busy() const { return busy_; }
  uint32_t staleCompletions() const { return staleCompletions_; }

private:
  void report(OperationPhase phase, Result result);

  MidiSendPort &port_;
  OutputObserver &observer_;
  BoundedQueue<OutputRequest, kOutputQueueCapacity> liveQueue_;
  BoundedQueue<OutputRequest, kOutputQueueCapacity> readbackQueue_;
  BoundedQueue<OutputRequest, kOutputQueueCapacity> backgroundQueue_;
  OutputRequest current_;
  SessionGeneration generation_ = 0;
  uint32_t submittedAt_ = 0;
  uint32_t nextAt_ = 0;
  uint32_t staleCompletions_ = 0;
  bool active_ = false;
  bool busy_ = false;
  bool pacingPending_ = false;
};

} // namespace stomp
