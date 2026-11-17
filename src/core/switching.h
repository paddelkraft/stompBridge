#pragma once

#include "bounded_queue.h"

#include <cstdint>

namespace stomp {

constexpr uint8_t kSwitchCount = 10;
constexpr uint8_t kMaxChords = 8;

enum class GestureKind : uint8_t { Press, Tap, Hold, Chord };
enum class BindingPolicy : uint8_t { Immediate, Deferred };

struct GestureEvent {
  GestureKind kind = GestureKind::Press;
  uint16_t switches = 0;
  uint32_t atMs = 0;
};

struct GestureTiming {
  uint16_t debounceMs = 25;
  uint16_t holdMs = 500;
  uint16_t chordMs = 60;
};

struct ChordBinding {
  uint16_t switches = 0;
  uint8_t priority = 0; // Lower number wins equal-size overlaps.
};

struct GestureConfig {
  GestureTiming timing;
  BindingPolicy policy[kSwitchCount] = {};
  ChordBinding chords[kMaxChords] = {};
  uint8_t chordCount = 0;
};

class Switching {
public:
  explicit Switching(const GestureConfig &config);
  bool valid() const { return valid_; }
  void sample(uint8_t id, bool pressed, uint32_t now);
  void tick(uint32_t now);
  bool pop(GestureEvent &event) { return events_.pop(event); }
  uint32_t droppedEvents() const { return droppedEvents_; }

private:
  struct SwitchState {
    bool initialized = false;
    bool raw = false;
    bool stable = false;
    bool startupHeld = false;
    bool consumed = false;
    bool tapPending = false;
    bool holdEmitted = false;
    uint32_t rawChangedAt = 0;
    uint32_t pressedAt = 0;
  };

  void edge(uint8_t id, bool pressed, uint32_t now);
  void emit(GestureKind kind, uint16_t mask, uint32_t now);
  void resolveChords(uint32_t now);
  static uint8_t bits(uint16_t mask);

  GestureConfig config_;
  SwitchState switches_[kSwitchCount] = {};
  BoundedQueue<GestureEvent, 16> events_;
  bool valid_ = false;
  bool chordPending_ = false;
  uint16_t chordLock_ = 0;
  uint32_t chordStartedAt_ = 0;
  uint32_t droppedEvents_ = 0;
};

} // namespace stomp
