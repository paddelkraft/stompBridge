#pragma once

#include "connection.h"
#include "device_plugin.h"
#include "owned_ingress.h"
#include "read_model.h"
#include "switching.h"
#include "toggle_policy.h"

#include "devices/registry.h"

namespace stomp {

struct Admission {
  Result result = Result::Invalid;
  OperationId operation = 0;
};

class ApplicationObserver {
public:
  virtual ~ApplicationObserver() = default;
  virtual void operationChanged(OperationId operation,
                                SessionGeneration generation,
                                OperationPhase phase, Result result) = 0;
};

class Application final : public OutputObserver, public DeviceUpdateSink {
public:
  Application(Clock &clock, MidiSendPort &port);
  void attach(const DeviceIdentity &identity);
  void disconnect(SessionGeneration generation);
  bool postMidi(SessionGeneration generation, const uint8_t *bytes, size_t length);
  bool postControl(const ControlEvent &event) { return ingress_.control(event); }
  void pump();
  void setObserver(ApplicationObserver *observer) { observer_ = observer; }
  Admission submit(CommandKind kind, uint16_t target = 0, bool enabled = false);
  Admission gesture(const GestureEvent &event);
  void bank(int8_t direction);
  const ReadSnapshot &snapshot() const { return model_.snapshot(); }
  bool outputBusy() const { return output_.busy(); }
  size_t outputQueued() const { return output_.queued(); }
  uint32_t droppedMidi() const { return ingress_.droppedMidi(); }
  uint32_t droppedControls() const { return ingress_.droppedControls(); }
  SessionGeneration generation() const { return manager_.generation(); }
  const char *deviceId() const {
    return manager_.device() ? manager_.device()->id : nullptr;
  }

  void outputEvent(const OutputEvent &event) override;
  void publish(const DeviceUpdate &update) override;
  void synchronizationStarted(SessionGeneration generation) override;
  void synchronization(SessionGeneration generation, bool verified,
                       Result outcome) override;

private:
  bool supported(CommandKind kind, uint16_t target) const;
  const BlockCapability *block(uint16_t id) const;
  void receiveLost();

  Clock &clock_;
  ConnectionManager manager_;
  SessionOutput output_;
  devices::PluginStorage storage_;
  ReadModel model_;
  OwnedIngress ingress_;
  DevicePlugin *plugin_ = nullptr;
  Command deferredCommand_;
  bool deferredCommandActive_ = false;
  OperationId nextOperation_ = 0;
  bool togglePending_ = false;
  ApplicationObserver *observer_ = nullptr;
  bool toggleTuner_ = false;
  uint16_t toggleTarget_ = 0;
  OperationId toggleOperation_ = 0;
};

} // namespace stomp
