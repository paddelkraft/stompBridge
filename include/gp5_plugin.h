#pragma once

#include "pedal_plugin.h"

class Gp5Plugin final : public PedalPlugin {
public:
  const char *id() const override { return "valeton_gp5"; }
  const char *name() const override { return "Valeton GP-5"; }
  bool matches(const MidiDeviceIdentity &identity) const override;
  void onConnected(MidiTransport &transport) override;
  void onDisconnected() override;
  void poll(MidiTransport &transport, uint32_t now) override;
  bool readyForAction(uint32_t now) const override;
  bool handleAction(PedalAction action, MidiTransport &transport,
                    uint32_t now) override;
  void handleMidi(const uint8_t *midi, size_t length, uint32_t now) override;
  const PedalState &state() const override { return state_; }
  void formatPreset(uint8_t index, char *output,
                    size_t outputSize) const override;
  const char *presetName(uint8_t index) const override;

private:
  enum class Request : uint8_t {
    None,
    SyncStart,
    PatchNames,
    CurrentPreset,
    PatchState,
  };

  void schedule(Request request, uint32_t now, uint32_t delayMs = 100);
  void scheduleStateRequest(uint32_t now, uint32_t delayMs = 350);
  bool sendRequest(Request request, MidiTransport &transport, uint32_t now);
  void updateBlockState(const uint8_t *payload, size_t length);

  PedalState state_;
  Request pendingRequest_ = Request::None;
  uint32_t requestAtMs_ = 0;
  bool syncInProgress_ = false;
  uint32_t syncStartedAtMs_ = 0;
  bool stateResponseInProgress_ = false;
  uint32_t stateResponseStartedMs_ = 0;
  uint8_t stateBuffer_[512] = {};
  size_t stateLength_ = 0;
  uint8_t nextFragment_ = 0;
  uint32_t patchNameFragments_ = 0;
};
