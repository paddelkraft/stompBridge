#pragma once

#include "core/device_plugin.h"
#include "protocol.h"

namespace stomp::pocket {

class Plugin final : public DevicePlugin {
public:
  Plugin(SessionOutput &output, DeviceUpdateSink &sink);
  const char *id() const override { return "pocket_master"; }
  const DeviceCapabilities &capabilities() const override { return capabilities_; }
  Result start(SessionGeneration generation, uint32_t now) override;
  void onMidi(const MidiMessage &message, uint32_t now) override;
  void onOutput(const OutputEvent &event, uint32_t now) override;
  void tick(uint32_t now) override;
  Result command(const Command &command, uint32_t now) override;
  bool readyForCommands() const override { return stage_ != Stage::Idle; }
  void stop() override;

private:
  enum class Stage : uint8_t { Idle, FirstPreset, Live, SecondPreset, Ready, Fault };
  Result requestPreset(uint32_t now);
  Result requestLive(uint32_t now);
  Result requestNames(uint32_t now);
  void consumeNames(const Frame &frame);
  void fail(Result reason = Result::TimedOut);
  void publishConfirmed(uint8_t preset, const LiveBlocks &blocks);

  SessionOutput &output_;
  DeviceUpdateSink &sink_;
  DeviceCapabilities capabilities_;
  LiveCollector collector_;
  NameDirectoryCollector names_;
  SessionGeneration generation_ = 0;
  Stage stage_ = Stage::Idle;
  uint32_t stageStartedAt_ = 0;
  OperationId internalOperation_ = 0x80000000u;
  bool stageSent_ = false;
  uint8_t firstPreset_ = 0;
  LiveBlocks liveBlocks_;
  Command pendingCommand_;
  bool pendingCommandActive_ = false;
  bool namesRequested_ = false;
  uint8_t nameAttempts_ = 0;
  uint32_t lastNameRequestAt_ = 0;
};

} // namespace stomp::pocket
