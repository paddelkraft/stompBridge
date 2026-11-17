#include "pocket_master_plugin.h"

#include <cstdio>

namespace {
constexpr uint16_t kValetonVendorId = 0x84ef;
constexpr uint16_t kPocketMasterProductId = 0x0284;

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

bool PocketMasterPlugin::matches(const MidiDeviceIdentity &identity) const {
  return identity.vendorId == kValetonVendorId &&
         identity.productId == kPocketMasterProductId;
}

void PocketMasterPlugin::onConnected(MidiTransport &) {
  state_ = PedalState{};
  Serial.println("Pocket Master plugin selected");
}

void PocketMasterPlugin::onDisconnected() {
  state_.presetCertainty = PedalCertainty::Unknown;
  state_.driveCertainty = PedalCertainty::Unknown;
  state_.delayCertainty = PedalCertainty::Unknown;
  state_.reverbCertainty = PedalCertainty::Unknown;
  state_.tunerCertainty = PedalCertainty::Unknown;
}

bool PocketMasterPlugin::handleAction(PedalAction action,
                                     MidiTransport &transport, uint32_t) {
  if (!transport.connected()) {
    return false;
  }

  if (action == PedalAction::BankDown || action == PedalAction::BankUp) {
    if (action == PedalAction::BankDown && state_.bank > 0) {
      --state_.bank;
    } else if (action == PedalAction::BankUp && state_.bank < 24) {
      ++state_.bank;
    }
    Serial.printf("Pocket Master bank %u (presets %u-%u)\n", state_.bank + 1,
                  state_.bank * 4 + 1, state_.bank * 4 + 4);
    return true;
  }

  uint8_t slot = 0;
  if (actionToPresetSlot(action, slot)) {
    const uint8_t presetIndex = state_.bank * 4 + slot;
    if (!transport.sendControlChange(1, 1,
                                     static_cast<uint8_t>(presetIndex + 1))) {
      Serial.println("Pocket Master preset send failed");
      return false;
    }
    state_.currentPreset = presetIndex;
    state_.presetCertainty = PedalCertainty::Assumed;
    Serial.printf("Pocket Master selected %s\n",
                  presetIndex < 50 ? "preset" : "effect slot");
    return true;
  }

  uint8_t control = 0;
  bool *enabled = nullptr;
  PedalCertainty *certainty = nullptr;
  switch (action) {
  case PedalAction::ToggleDrive:
    control = 45;
    enabled = &state_.driveEnabled;
    certainty = &state_.driveCertainty;
    break;
  case PedalAction::ToggleDelay:
    control = 50;
    enabled = &state_.delayEnabled;
    certainty = &state_.delayCertainty;
    break;
  case PedalAction::ToggleReverb:
    control = 51;
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
    Serial.printf("Pocket Master CC%u send failed\n", control);
    return false;
  }
  *enabled = target;
  *certainty = PedalCertainty::Assumed;
  Serial.printf("Pocket Master CC%u=%u sent\n", control,
                target ? 127 : 0);
  return true;
}

void PocketMasterPlugin::formatPreset(uint8_t index, char *output,
                                      size_t outputSize) const {
  if (index < 50) {
    snprintf(output, outputSize, "P%02u", static_cast<unsigned>(index + 1));
  } else {
    snprintf(output, outputSize, "F%02u", static_cast<unsigned>(index - 49));
  }
}
