#pragma once

#include "core/device_plugin.h"
#include "readback.h"

namespace stomp::gp5 {

class Plugin final : public DevicePlugin {
public:
  Plugin(SessionOutput &output, DeviceUpdateSink &sink);
  const char *id() const override { return "valeton_gp5"; }
  const DeviceCapabilities &capabilities() const override { return capabilities_; }
  Result start(SessionGeneration generation, uint32_t now) override;
  void onMidi(const MidiMessage &message, uint32_t now) override;
  void onOutput(const OutputEvent &event, uint32_t now) override;
  void tick(uint32_t now) override;
  Result command(const Command &command, uint32_t now) override;
  bool readyForCommands() const override { return stage_ != Stage::Idle; }
  void stop() override;

private:
  enum class Stage : uint8_t { Idle, FirstPreset, PatchState, SecondPreset, Ready, Fault };
  Result requestPreset(uint32_t now);
  Result requestPatch(uint32_t now);
  Result requestNames(uint32_t now);
  void collectModelFragment(const MidiMessage &message);
  void publishModels();
  void consumeNames(const MidiMessage &message);
  void fail(Result reason = Result::TimedOut);
  void publishConfirmed(uint8_t preset, const Blocks &blocks, uint32_t now);

  SessionOutput &output_;
  DeviceUpdateSink &sink_;
  DeviceCapabilities capabilities_;
  SessionGeneration generation_ = 0;
  Stage stage_ = Stage::Idle;
  uint32_t stageStartedAt_ = 0;
  OperationId internalOperation_ = 0x90000000u;
  bool stageSent_ = false;
  uint8_t firstPreset_ = 0;
  Blocks blocks_;
  Command pendingCommand_;
  bool pendingCommandActive_ = false;
  bool namesRequested_ = false;
  bool namesComplete_ = false;
  uint8_t nameAttempts_ = 0;
  uint32_t lastNameRequestAt_ = 0;
  bool nameFragments_[106] = {};
  uint8_t nameFragmentCount_ = 0;
  uint8_t nameLogical_[2002] = {};
  uint8_t modelLogical_[133] = {};
  uint8_t nextModelFragment_ = 0;
  uint32_t modelIds_[3] = {};
  bool modelReady_ = false;
  bool modelPublished_ = false;
};

} // namespace stomp::gp5
