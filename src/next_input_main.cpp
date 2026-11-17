#include <Arduino.h>

#include "board_pins.h"
#include "core/switching.h"

namespace {

stomp::GestureConfig inputConfig() {
  stomp::GestureConfig config;
  config.policy[0] = stomp::BindingPolicy::Deferred;
  config.policy[1] = stomp::BindingPolicy::Deferred;
  config.policy[9] = stomp::BindingPolicy::Deferred;
  config.chordCount = 1;
  config.chords[0] = {0b0000000011, 0};
  return config;
}

stomp::Switching switching(inputConfig());

const char *gestureName(stomp::GestureKind kind) {
  switch (kind) {
  case stomp::GestureKind::Press: return "PRESS";
  case stomp::GestureKind::Tap: return "TAP";
  case stomp::GestureKind::Hold: return "HOLD";
  case stomp::GestureKind::Chord: return "CHORD";
  }
  return "UNKNOWN";
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  const uint32_t now = millis();
  for (size_t id = 0; id < BoardPins::kSwitchCount; ++id) {
    pinMode(BoardPins::kSwitches[id], INPUT_PULLUP);
    switching.sample(static_cast<uint8_t>(id),
                     digitalRead(BoardPins::kSwitches[id]) == LOW, now);
  }
  Serial.println("StompBridge replacement input diagnostic");
  Serial.println("No pedal commands or Wi-Fi service in this diagnostic build");
}

void loop() {
  const uint32_t now = millis();
  for (size_t id = 0; id < BoardPins::kSwitchCount; ++id) {
    switching.sample(static_cast<uint8_t>(id),
                     digitalRead(BoardPins::kSwitches[id]) == LOW, now);
  }
  switching.tick(now);
  stomp::GestureEvent event;
  while (switching.pop(event)) {
    Serial.printf("INPUT %s mask=%04X at=%lu\n", gestureName(event.kind),
                  static_cast<unsigned>(event.switches),
                  static_cast<unsigned long>(event.atMs));
  }
  delay(1);
}
