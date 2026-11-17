#pragma once

#include "contracts.h"
#include "session_output.h"

namespace stomp {

enum class DeviceUpdateKind : uint8_t {
  Preset, Block, Tuner, PresetName, NamesComplete, BlockModel
};

struct DeviceUpdate {
  DeviceUpdateKind kind = DeviceUpdateKind::Preset;
  SessionGeneration generation = 0;
  uint16_t target = 0;
  bool enabled = false;
  Certainty certainty = Certainty::Unknown;
  uint32_t modelId = 0;
  char name[kMaxBlockLabel] = {};
};

class DeviceUpdateSink {
public:
  virtual ~DeviceUpdateSink() = default;
  virtual void publish(const DeviceUpdate &update) = 0;
  virtual void synchronizationStarted(SessionGeneration generation) = 0;
  virtual void synchronization(SessionGeneration generation, bool verified,
                               Result outcome) = 0;
};

class DevicePlugin {
public:
  virtual ~DevicePlugin() = default;
  virtual const char *id() const = 0;
  virtual const DeviceCapabilities &capabilities() const = 0;
  virtual Result start(SessionGeneration generation, uint32_t now) = 0;
  virtual void onMidi(const MidiMessage &message, uint32_t now) = 0;
  virtual void onOutput(const OutputEvent &event, uint32_t now) = 0;
  virtual void tick(uint32_t now) = 0;
  virtual Result command(const Command &command, uint32_t now) = 0;
  virtual bool readyForCommands() const = 0;
  virtual void stop() = 0;
};

} // namespace stomp
