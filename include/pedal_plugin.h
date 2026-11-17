#pragma once

#include <Arduino.h>

#include "midi_transport.h"

enum class PedalAction : uint8_t {
  BankDown,
  BankUp,
  ToggleDrive,
  ToggleDelay,
  ToggleReverb,
  PresetA,
  PresetB,
  PresetC,
  PresetD,
  ToggleTuner,
};

enum class PedalCertainty : uint8_t { Unknown, Assumed, Confirmed };

struct PedalState {
  uint8_t bank = 0;
  uint8_t currentPreset = 0;
  char patchNames[100][11] = {};
  bool driveEnabled = false;
  bool delayEnabled = false;
  bool reverbEnabled = false;
  bool tunerEnabled = false;
  PedalCertainty presetCertainty = PedalCertainty::Unknown;
  PedalCertainty driveCertainty = PedalCertainty::Unknown;
  PedalCertainty delayCertainty = PedalCertainty::Unknown;
  PedalCertainty reverbCertainty = PedalCertainty::Unknown;
  PedalCertainty tunerCertainty = PedalCertainty::Unknown;
  char driveModelName[24] = "Drive";
  char delayModelName[24] = "Delay";
  char reverbModelName[24] = "Reverb";
};

// A pedal plugin owns device-specific MIDI commands, synchronization, and
// decoding. The transport only handles MIDI framing and USB/BLE connectivity.
class PedalPlugin {
public:
  virtual ~PedalPlugin() = default;
  virtual const char *id() const = 0;
  virtual const char *name() const = 0;
  virtual bool matches(const MidiDeviceIdentity &identity) const = 0;
  virtual void onConnected(MidiTransport &transport) = 0;
  virtual void onDisconnected() = 0;
  virtual void poll(MidiTransport &transport, uint32_t now) = 0;
  virtual bool readyForAction(uint32_t now) const = 0;
  virtual bool handleAction(PedalAction action, MidiTransport &transport,
                            uint32_t now) = 0;
  virtual void handleMidi(const uint8_t *midi, size_t length,
                          uint32_t now) = 0;
  virtual const PedalState &state() const = 0;
  virtual void formatPreset(uint8_t index, char *output,
                           size_t outputSize) const = 0;
  virtual const char *presetName(uint8_t index) const = 0;
};

PedalPlugin *findPedalPlugin(const MidiDeviceIdentity &identity);
const char *pedalCertaintyName(PedalCertainty certainty);
