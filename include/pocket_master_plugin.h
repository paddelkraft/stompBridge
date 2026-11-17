#pragma once

#include "pedal_plugin.h"

class PocketMasterPlugin final : public PedalPlugin {
public:
  const char *id() const override { return "pocket_master"; }
  const char *name() const override { return "Pocket Master"; }
  bool matches(const MidiDeviceIdentity &identity) const override;
  void onConnected(MidiTransport &transport) override;
  void onDisconnected() override;
  void poll(MidiTransport &, uint32_t) override {}
  bool readyForAction(uint32_t) const override { return true; }
  bool handleAction(PedalAction action, MidiTransport &transport,
                    uint32_t now) override;
  void handleMidi(const uint8_t *, size_t, uint32_t) override {}
  const PedalState &state() const override { return state_; }
  void formatPreset(uint8_t index, char *output,
                    size_t outputSize) const override;
  const char *presetName(uint8_t) const override { return ""; }

private:
  PedalState state_;
};
