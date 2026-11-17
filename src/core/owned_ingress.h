#pragma once

#include "bounded_queue.h"
#include "contracts.h"

#include <cstring>

namespace stomp {

// Called only inside a platform-provided critical section when producers can
// run on different tasks. The application context is the sole consumer.
class OwnedIngress {
public:
  explicit OwnedIngress(SessionGeneration generation = 0)
      : generation_(generation) {}

  void beginSession(SessionGeneration generation) {
    generation_ = generation;
    midi_.clear();
    controls_.clear();
    disconnectPending_ = false;
    receiveLostPending_ = false;
    closed_ = false;
  }

  bool midi(SessionGeneration generation, const uint8_t *bytes, size_t length) {
    if (generation != generation_ || closed_) {
      ++staleEvents_;
      return false;
    }
    if (bytes == nullptr || length == 0 || length > kMaxMidiMessage ||
        midi_.size() == midi_.capacity()) {
      midi_.clear();
      receiveLostPending_ = true;
      ++droppedMidi_;
      return false;
    }
    MidiMessage message;
    message.generation = generation;
    message.length = static_cast<uint16_t>(length);
    memcpy(message.bytes, bytes, length);
    return midi_.push(message);
  }

  bool control(const ControlEvent &event) {
    if (event.generation != generation_) {
      ++staleEvents_;
      return false;
    }
    if (event.kind == ControlEventKind::Disconnected) {
      midi_.clear();
      controls_.clear();
      disconnectPending_ = true;
      receiveLostPending_ = false;
      closed_ = true;
      return true;
    }
    if (closed_) return false;
    if (!controls_.push(event)) {
      receiveLostPending_ = true;
      ++droppedControls_;
      return false;
    }
    return true;
  }

  bool popControl(ControlEvent &event) {
    if (disconnectPending_) {
      disconnectPending_ = false;
      event = {ControlEventKind::Disconnected, generation_, 0, false};
      return true;
    }
    if (receiveLostPending_) {
      receiveLostPending_ = false;
      event = {ControlEventKind::ReceiveLost, generation_, 0, false};
      return true;
    }
    return controls_.pop(event);
  }

  bool popMidi(MidiMessage &message) {
    if (disconnectPending_ || receiveLostPending_) return false;
    return midi_.pop(message);
  }

  uint32_t staleEvents() const { return staleEvents_; }
  uint32_t droppedMidi() const { return droppedMidi_; }
  uint32_t droppedControls() const { return droppedControls_; }
  size_t queuedMidi() const { return midi_.size(); }

private:
  SessionGeneration generation_ = 0;
  BoundedQueue<MidiMessage, kMidiIngressCapacity> midi_;
  BoundedQueue<ControlEvent, kControlIngressCapacity> controls_;
  bool disconnectPending_ = false;
  bool receiveLostPending_ = false;
  bool closed_ = false;
  uint32_t staleEvents_ = 0;
  uint32_t droppedMidi_ = 0;
  uint32_t droppedControls_ = 0;
};

} // namespace stomp
