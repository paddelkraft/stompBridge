#include "gp5_plugin.h"

#include <cstdio>
#include <cstring>

#include "gp5_models.h"
#include "gp5_protocol.h"

namespace {
constexpr uint16_t kValetonVendorId = 0x84ef;
constexpr uint16_t kGp5ProductId = 0x0184;

void copyModelName(char *destination, size_t capacity, const char *name) {
  if (name != nullptr && capacity > 0) {
    snprintf(destination, capacity, "%s", name);
  }
}

bool actionToPresetSlot(PedalAction action, uint8_t &slot) {
  switch (action) {
  case PedalAction::PresetA:
    slot = 0;
    return true;
  case PedalAction::PresetB:
    slot = 1;
    return true;
  case PedalAction::PresetC:
    slot = 2;
    return true;
  case PedalAction::PresetD:
    slot = 3;
    return true;
  default:
    return false;
  }
}
} // namespace

bool Gp5Plugin::matches(const MidiDeviceIdentity &identity) const {
  return identity.vendorId == kValetonVendorId &&
         identity.productId == kGp5ProductId;
}

void Gp5Plugin::onConnected(MidiTransport &) {
  state_ = PedalState{};
  patchNameFragments_ = 0;
  stateLength_ = 0;
  nextFragment_ = 0;
  stateResponseInProgress_ = false;
  syncInProgress_ = true;
  syncStartedAtMs_ = millis();
  schedule(Request::SyncStart, millis());
  Serial.println("Valeton GP-5 plugin selected; starting GP-5 sync");
}

void Gp5Plugin::onDisconnected() {
  pendingRequest_ = Request::None;
  syncInProgress_ = false;
  stateResponseInProgress_ = false;
  stateLength_ = 0;
  nextFragment_ = 0;
  state_.presetCertainty = PedalCertainty::Unknown;
  state_.driveCertainty = PedalCertainty::Unknown;
  state_.delayCertainty = PedalCertainty::Unknown;
  state_.reverbCertainty = PedalCertainty::Unknown;
  state_.tunerCertainty = PedalCertainty::Unknown;
}

void Gp5Plugin::schedule(Request request, uint32_t now, uint32_t delayMs) {
  pendingRequest_ = request;
  requestAtMs_ = now + delayMs;
}

void Gp5Plugin::scheduleStateRequest(uint32_t now, uint32_t delayMs) {
  if (!syncInProgress_) {
    schedule(Request::PatchState, now, delayMs);
  }
}

bool Gp5Plugin::readyForAction(uint32_t now) const {
  return !syncInProgress_ &&
         (!stateResponseInProgress_ || now - stateResponseStartedMs_ >= 1000);
}

bool Gp5Plugin::sendRequest(Request request, MidiTransport &transport,
                            uint32_t now) {
  uint8_t message[14];
  size_t length = 0;
  const char *description = nullptr;
  switch (request) {
  case Request::SyncStart:
    length = buildGp5SyncStartRequest(message, sizeof(message));
    description = "synchronization start";
    break;
  case Request::PatchNames:
    length = buildGp5PatchListRequest(message, sizeof(message));
    description = "patch names";
    break;
  case Request::CurrentPreset:
    length = buildGp5CurrentPresetRequest(message, sizeof(message));
    description = "current patch number";
    break;
  case Request::PatchState:
    length = buildGp5PatchStateRequest(message, sizeof(message));
    description = "current patch state";
    break;
  case Request::None:
    return false;
  }

  if (length == 0 || !transport.send(message, length)) {
    schedule(request, now, 500);
    return false;
  }
  Serial.printf("GP-5 plugin requested %s\n", description);
  if (request == Request::SyncStart) {
    schedule(Request::PatchNames, now, 100);
  }
  return true;
}

void Gp5Plugin::poll(MidiTransport &transport, uint32_t now) {
  if (syncInProgress_ && now - syncStartedAtMs_ >= 5000) {
    syncInProgress_ = false;
    state_.presetCertainty = PedalCertainty::Unknown;
    Serial.println("GP-5 plugin sync timed out; enabling footswitch actions");
  }
  if (stateResponseInProgress_ && now - stateResponseStartedMs_ >= 1000) {
    stateResponseInProgress_ = false;
  }
  if (!transport.connected() || pendingRequest_ == Request::None ||
      static_cast<int32_t>(now - requestAtMs_) < 0) {
    return;
  }
  const Request request = pendingRequest_;
  pendingRequest_ = Request::None;
  sendRequest(request, transport, now);
}

bool Gp5Plugin::handleAction(PedalAction action, MidiTransport &transport,
                             uint32_t now) {
  if (!transport.connected()) {
    return false;
  }

  if (action == PedalAction::BankDown || action == PedalAction::BankUp) {
    if (action == PedalAction::BankDown && state_.bank > 0) {
      --state_.bank;
    } else if (action == PedalAction::BankUp && state_.bank < 24) {
      ++state_.bank;
    }
    Serial.printf("GP-5 bank %u (presets %u-%u)\n", state_.bank + 1,
                  state_.bank * 4 + 1, state_.bank * 4 + 4);
    return true;
  }

  uint8_t slot = 0;
  if (actionToPresetSlot(action, slot)) {
    const uint8_t presetIndex = state_.bank * 4 + slot;
    uint8_t message[24];
    const size_t length =
        buildGp5PresetChange(presetIndex, message, sizeof(message));
    if (length == 0 || !transport.send(message, length)) {
      Serial.println("GP-5 plugin preset send failed");
      return false;
    }
    state_.currentPreset = presetIndex;
    state_.presetCertainty = PedalCertainty::Assumed;
    Serial.printf("GP-5 plugin selected preset P%02u\n", presetIndex + 1);
    scheduleStateRequest(now, 350);
    return true;
  }

  uint8_t control = 0;
  bool *enabled = nullptr;
  PedalCertainty *certainty = nullptr;
  switch (action) {
  case PedalAction::ToggleDrive:
    control = 50;
    enabled = &state_.driveEnabled;
    certainty = &state_.driveCertainty;
    break;
  case PedalAction::ToggleDelay:
    control = 56;
    enabled = &state_.delayEnabled;
    certainty = &state_.delayCertainty;
    break;
  case PedalAction::ToggleReverb:
    control = 57;
    enabled = &state_.reverbEnabled;
    certainty = &state_.reverbCertainty;
    break;
  case PedalAction::ToggleTuner:
    control = 58;
    enabled = &state_.tunerEnabled;
    certainty = &state_.tunerCertainty;
    break;
  default:
    return false;
  }

  const bool target = !*enabled;
  if (!transport.sendControlChange(1, control, target ? 127 : 0)) {
    Serial.printf("GP-5 plugin CC%u send failed\n", control);
    return false;
  }
  *enabled = target;
  *certainty = PedalCertainty::Assumed;
  Serial.printf("GP-5 plugin CC%u=%u sent\n", control, target ? 127 : 0);
  scheduleStateRequest(now);
  return true;
}

void Gp5Plugin::handleMidi(const uint8_t *midi, size_t length, uint32_t now) {
  uint8_t command = 0;
  uint8_t fragment = 0;
  uint8_t payload[128];
  size_t payloadLength = 0;
  if (!decodeGp5Fragment(midi, length, command, fragment, payload,
                         sizeof(payload), payloadLength)) {
    Serial.printf("GP-5 plugin ignored notification (%u MIDI bytes)\n",
                  static_cast<unsigned>(length));
    const size_t shown = length < 64 ? length : 64;
    Serial.print("GP-5 raw MIDI:");
    for (size_t i = 0; i < shown; ++i) {
      Serial.printf(" %02X", midi[i]);
    }
    if (shown < length) Serial.print(" ...");
    Serial.println();
    return;
  }

  Serial.printf("GP-5 plugin response: command %u, fragment %u, payload %u bytes\n",
                static_cast<unsigned>(command), static_cast<unsigned>(fragment),
                static_cast<unsigned>(payloadLength));
  const size_t shown = length < 64 ? length : 64;
  Serial.print("GP-5 response frame:");
  for (size_t i = 0; i < shown; ++i) {
    Serial.printf(" %02X", midi[i]);
  }
  if (shown < length) Serial.print(" ...");
  Serial.println();

  if (command == 0x15) {
    if (fragment < 20 && payloadLength == 100) {
      char names[5][11];
      if (decodeGp5PatchNames(fragment, payload, payloadLength, names)) {
        for (size_t i = 0; i < 5; ++i) {
          memcpy(state_.patchNames[fragment * 5 + i], names[i],
                 sizeof(names[i]));
        }
        patchNameFragments_ |= 1u << fragment;
        if (patchNameFragments_ == ((1u << 20) - 1)) {
          Serial.println("GP-5 plugin synchronized patch names");
          schedule(Request::CurrentPreset, now, 50);
        }
      }
    } else if (fragment >= 20) {
      schedule(Request::CurrentPreset, now, 50);
    }
    return;
  }

  if (command == 1) {
    uint8_t confirmedPreset = 0;
    if (decodeGp5CurrentPreset(payload, payloadLength, confirmedPreset)) {
      state_.currentPreset = confirmedPreset;
      state_.bank = confirmedPreset / 4;
      state_.presetCertainty = PedalCertainty::Confirmed;
      syncInProgress_ = false;
      Serial.printf("GP-5 plugin confirmed preset P%02u\n",
                    static_cast<unsigned>(confirmedPreset + 1));
      scheduleStateRequest(now, 150);
    }
    return;
  }

  if (command != 5) {
    return;
  }
  if (fragment == 0) {
    stateLength_ = 0;
    nextFragment_ = 0;
    stateResponseInProgress_ = true;
    stateResponseStartedMs_ = now;
  }
  if (fragment != nextFragment_ ||
      payloadLength > sizeof(stateBuffer_) - stateLength_) {
    stateLength_ = 0;
    nextFragment_ = 0;
    return;
  }
  memcpy(stateBuffer_ + stateLength_, payload, payloadLength);
  stateLength_ += payloadLength;
  ++nextFragment_;
  if (payloadLength == 100) {
    return;
  }

  Gp5PatchDetails details;
  if (decodeGp5PatchDetails(stateBuffer_, stateLength_, details)) {
    state_.driveEnabled = details.driveEnabled;
    state_.delayEnabled = details.delayEnabled;
    state_.reverbEnabled = details.reverbEnabled;
    copyModelName(state_.driveModelName, sizeof(state_.driveModelName),
                  gp5EffectModelName(2, details.driveModelId));
    copyModelName(state_.delayModelName, sizeof(state_.delayModelName),
                  gp5EffectModelName(7, details.delayModelId));
    copyModelName(state_.reverbModelName, sizeof(state_.reverbModelName),
                  gp5EffectModelName(8, details.reverbModelId));
    state_.driveCertainty = PedalCertainty::Confirmed;
    state_.delayCertainty = PedalCertainty::Confirmed;
    state_.reverbCertainty = PedalCertainty::Confirmed;
    Serial.printf("GP-5 plugin state: DRIVE %s (%s), DELAY %s (%s), REVERB %s (%s)\n",
                  state_.driveEnabled ? "ON" : "OFF", state_.driveModelName,
                  state_.delayEnabled ? "ON" : "OFF", state_.delayModelName,
                  state_.reverbEnabled ? "ON" : "OFF", state_.reverbModelName);
  }
  stateLength_ = 0;
  nextFragment_ = 0;
  stateResponseInProgress_ = false;
}

void Gp5Plugin::formatPreset(uint8_t index, char *output,
                             size_t outputSize) const {
  snprintf(output, outputSize, "P%02u", static_cast<unsigned>(index + 1));
}

const char *Gp5Plugin::presetName(uint8_t index) const {
  return index < 100 && state_.patchNames[index][0] != '\0'
             ? state_.patchNames[index]
             : "";
}
