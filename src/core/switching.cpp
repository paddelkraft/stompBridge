#include "switching.h"

namespace stomp {

uint8_t Switching::bits(uint16_t mask) {
  uint8_t count = 0;
  while (mask) {
    count += mask & 1u;
    mask >>= 1;
  }
  return count;
}

Switching::Switching(const GestureConfig &config) : config_(config) {
  valid_ = config.chordCount <= kMaxChords && config.timing.debounceMs > 0 &&
           config.timing.holdMs > config.timing.chordMs;
  for (uint8_t i = 0; valid_ && i < config.chordCount; ++i) {
    const uint16_t mask = config.chords[i].switches;
    valid_ = bits(mask) >= 2 && (mask & ~uint16_t(0x03ff)) == 0;
    for (uint8_t id = 0; id < kSwitchCount; ++id) {
      if ((mask & (1u << id)) && config.policy[id] != BindingPolicy::Deferred)
        valid_ = false;
    }
    for (uint8_t j = 0; j < i; ++j) {
      if (config.chords[j].switches == mask) valid_ = false;
      if ((config.chords[j].switches & mask) &&
          bits(config.chords[j].switches) == bits(mask) &&
          config.chords[j].priority == config.chords[i].priority)
        valid_ = false;
    }
  }
}

void Switching::emit(GestureKind kind, uint16_t mask, uint32_t now) {
  if (!events_.push({kind, mask, now})) ++droppedEvents_;
}

void Switching::sample(uint8_t id, bool pressed, uint32_t now) {
  if (!valid_ || id >= kSwitchCount) return;
  SwitchState &state = switches_[id];
  if (!state.initialized) {
    state.initialized = true;
    state.raw = state.stable = pressed;
    state.startupHeld = pressed;
    state.rawChangedAt = now;
    return;
  }
  if (pressed != state.raw) {
    state.raw = pressed;
    state.rawChangedAt = now;
  }
  if (state.stable != state.raw &&
      uint32_t(now - state.rawChangedAt) >= config_.timing.debounceMs) {
    state.stable = state.raw;
    edge(id, state.stable, now);
  }
}

void Switching::edge(uint8_t id, bool pressed, uint32_t now) {
  SwitchState &state = switches_[id];
  if (!pressed) {
    if (state.startupHeld) {
      state.startupHeld = false;
    } else if (!state.consumed && !state.holdEmitted &&
               config_.policy[id] == BindingPolicy::Deferred) {
      state.tapPending = true;
    }
    if (!(chordLock_ & (1u << id))) state.consumed = false;
    state.holdEmitted = false;
    if (chordLock_) {
      bool anyHeld = false;
      for (uint8_t member = 0; member < kSwitchCount; ++member)
        if ((chordLock_ & (1u << member)) && switches_[member].stable)
          anyHeld = true;
      if (!anyHeld) {
        for (uint8_t member = 0; member < kSwitchCount; ++member)
          if (chordLock_ & (1u << member)) switches_[member].consumed = false;
        chordLock_ = 0;
      }
    }
    return;
  }
  state.pressedAt = now;
  state.tapPending = false;
  state.consumed = state.startupHeld || (chordLock_ & (1u << id));
  if (state.startupHeld) return;
  if (config_.policy[id] == BindingPolicy::Immediate) {
    emit(GestureKind::Press, uint16_t(1u << id), now);
  } else if (!chordPending_) {
    chordPending_ = true;
    chordStartedAt_ = now;
  }
}

void Switching::resolveChords(uint32_t now) {
  if (!chordPending_ ||
      uint32_t(now - chordStartedAt_) < config_.timing.chordMs) return;
  chordPending_ = false;
  int chosen = -1;
  uint8_t chosenSize = 0;
  for (uint8_t i = 0; i < config_.chordCount; ++i) {
    const uint16_t mask = config_.chords[i].switches;
    bool complete = true;
    for (uint8_t id = 0; id < kSwitchCount; ++id) {
      if (!(mask & (1u << id))) continue;
      const auto &state = switches_[id];
      if ((!state.stable && !state.tapPending) || state.consumed ||
          uint32_t(state.pressedAt - chordStartedAt_) > config_.timing.chordMs)
        complete = false;
    }
    const uint8_t size = bits(mask);
    if (complete && (chosen < 0 || size > chosenSize ||
                     (size == chosenSize &&
                      config_.chords[i].priority < config_.chords[chosen].priority))) {
      chosen = i;
      chosenSize = size;
    }
  }
  if (chosen >= 0) {
    const uint16_t mask = config_.chords[chosen].switches;
    emit(GestureKind::Chord, mask, now);
    chordLock_ = mask;
    for (uint8_t id = 0; id < kSwitchCount; ++id) {
      if (mask & (1u << id)) {
        switches_[id].consumed = true;
        switches_[id].tapPending = false;
      }
    }
  }
}

void Switching::tick(uint32_t now) {
  if (!valid_) return;
  resolveChords(now);
  for (uint8_t id = 0; id < kSwitchCount; ++id) {
    SwitchState &state = switches_[id];
    if (config_.policy[id] != BindingPolicy::Deferred || state.startupHeld ||
        state.consumed) continue;
    if (state.stable && !state.holdEmitted && !chordPending_ &&
        uint32_t(now - state.pressedAt) >= config_.timing.holdMs) {
      state.holdEmitted = true;
      emit(GestureKind::Hold, uint16_t(1u << id), now);
    } else if (state.tapPending && !chordPending_) {
      state.tapPending = false;
      emit(GestureKind::Tap, uint16_t(1u << id), now);
    }
  }
}

} // namespace stomp
