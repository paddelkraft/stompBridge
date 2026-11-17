#include <Arduino.h>

#include "board_pins.h"
#include "core/application.h"
#include "core/switching.h"
#include "usb_midi_transport.h"

namespace {

// GP-5 can return many SysEx fragments in one burst before the loop task runs.
constexpr UBaseType_t kEventCapacity = 32;

struct PlatformClock final : stomp::Clock {
  uint32_t nowMs() const override { return millis(); }
};

class UsbSendPort final : public stomp::MidiSendPort {
public:
  explicit UsbSendPort(UsbMidiTransport &transport) : transport_(transport) {}

  void generation(stomp::SessionGeneration value) {
    portENTER_CRITICAL(&mux_);
    generation_ = value;
    portEXIT_CRITICAL(&mux_);
  }

  bool submit(const uint8_t *bytes, size_t length) override {
    if (length >= 4 && bytes[0] == 0xf0 && bytes[1] == 0x03 &&
        bytes[2] == 0x05)
      Serial.println("PATCH LIST request submitted");
    if (length >= 4 && bytes[0] == 0xf0 && bytes[1] == 0x00 &&
        bytes[2] == 0x0e)
      Serial.println("GP-5 sync-start request submitted");
    portENTER_CRITICAL(&mux_);
    inFlight_ = generation_;
    portEXIT_CRITICAL(&mux_);
    if (transport_.send(bytes, length)) return true;
    portENTER_CRITICAL(&mux_);
    inFlight_ = 0;
    portEXIT_CRITICAL(&mux_);
    return false;
  }

  stomp::SessionGeneration takeCompletionGeneration() {
    portENTER_CRITICAL(&mux_);
    const auto value = inFlight_;
    inFlight_ = 0;
    portEXIT_CRITICAL(&mux_);
    return value;
  }

private:
  UsbMidiTransport &transport_;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  stomp::SessionGeneration generation_ = 0;
  stomp::SessionGeneration inFlight_ = 0;
};

enum class PlatformEventKind : uint8_t { Midi, SendComplete, ReceiveLost };

struct PlatformEvent {
  PlatformEventKind kind = PlatformEventKind::Midi;
  stomp::SessionGeneration generation = 0;
  uint16_t length = 0;
  bool success = false;
  uint8_t bytes[stomp::kMaxMidiMessage] = {};
};

class UsbApplicationAdapter {
public:
  UsbApplicationAdapter(UsbMidiTransport &transport, UsbSendPort &port,
                        stomp::Application &application)
      : transport_(transport), port_(port), application_(application) {}

  bool begin() {
    events_ = xQueueCreate(kEventCapacity, sizeof(PlatformEvent));
    if (events_ == nullptr) return false;
    Serial.printf("USB ingress capacity=%u freeHeap=%u\n",
                  static_cast<unsigned>(kEventCapacity),
                  static_cast<unsigned>(ESP.getFreeHeap()));
    transport_.setReceiveHandler(received, this);
    transport_.setEventHandler(status, this);
    return transport_.begin();
  }

  void poll() {
    if (events_ == nullptr) return;
    transport_.poll();
    MidiDeviceIdentity identity;
    const bool connected = transport_.connectedDevice(identity);
    if (attached_ && (!connected || identity.vendorId != vendorId_ ||
                      identity.productId != productId_)) {
      activeGeneration(0);
      application_.disconnect(application_.generation());
      attached_ = false;
      Serial.println("Replacement USB session disconnected");
    }
    if (!attached_ && connected) {
      application_.attach({stomp::IdentityKind::UsbDescriptor,
                           identity.vendorId, identity.productId, 0});
      vendorId_ = identity.vendorId;
      productId_ = identity.productId;
      attached_ = true;
      tracesSeen_ = 0;
      traceRead_ = 0;
      traceWrite_ = 0;
      activeGeneration(application_.generation());
      port_.generation(application_.generation());
      Serial.printf("Replacement USB identity %04X:%04X phase=%u\n",
                    vendorId_, productId_,
                    static_cast<unsigned>(application_.snapshot().connection));
    }

    stomp::SessionGeneration lostGeneration;
    uint8_t lostReason;
    portENTER_CRITICAL(&mux_);
    lostGeneration = lostGeneration_;
    lostReason = lostReason_;
    lostGeneration_ = 0;
    lostReason_ = 0;
    portEXIT_CRITICAL(&mux_);
    if (lostGeneration != 0 && lostGeneration == application_.generation()) {
      Serial.printf("USB ingress lost gen=%lu reason=%s\n",
                    static_cast<unsigned long>(lostGeneration),
                    lostReason == 1 ? "event-queue-full" : "invalid-midi-length");
      xQueueReset(events_);
      application_.postControl({stomp::ControlEventKind::ReceiveLost,
                                lostGeneration, 0, false});
    } else {
      PlatformEvent event;
      while (xQueueReceive(events_, &event, 0) == pdTRUE) {
        if (event.kind == PlatformEventKind::Midi) {
          ++receivedFrames_;
          traceMidi(event);
          application_.postMidi(event.generation, event.bytes, event.length);
        } else {
          if (event.kind == PlatformEventKind::SendComplete) ++sendCompletions_;
          application_.postControl({
              event.kind == PlatformEventKind::SendComplete
                  ? stomp::ControlEventKind::SendComplete
                  : stomp::ControlEventKind::ReceiveLost,
              event.generation, 0, event.success});
        }
        // Keep the smaller application ingress drained during a pedal burst.
        application_.pump();
      }
    }
    application_.pump();
    flushTrace();
  }

  uint32_t receivedFrames() const { return receivedFrames_; }
  uint32_t sendCompletions() const { return sendCompletions_; }
  void resetTrace() {
    tracesSeen_ = 0;
    traceRead_ = 0;
    traceWrite_ = 0;
  }
  size_t pendingEvents() const {
    return events_ ? uxQueueMessagesWaiting(events_) : 0;
  }

private:
  struct MidiTrace {
    uint16_t length = 0;
    bool nameFrame = false;
    uint8_t first[16] = {};
    uint8_t tail[4] = {};
    uint8_t bitmap[4] = {};
  };

  void traceMidi(const PlatformEvent &event) {
    const auto &state = application_.snapshot();
    const bool nameFrame = false;
    if ((state.sync != stomp::SyncState::Pending && !nameFrame) ||
        tracesSeen_ >= 20 || traceWrite_ - traceRead_ >= 20) return;
    ++tracesSeen_;
    MidiTrace &trace = traces_[traceWrite_ % 20];
    trace.length = event.length;
    trace.nameFrame = nameFrame;
    const size_t firstCount = event.length < sizeof(trace.first)
                                  ? event.length : sizeof(trace.first);
    memcpy(trace.first, event.bytes, firstCount);
    if (event.length >= sizeof(trace.tail))
      memcpy(trace.tail, event.bytes + event.length - sizeof(trace.tail),
             sizeof(trace.tail));
    if (event.length >= 39) memcpy(trace.bitmap, event.bytes + 35, 4);
    else memset(trace.bitmap, 0, sizeof(trace.bitmap));
    ++traceWrite_;
  }

  void flushTrace() {
    if (application_.snapshot().sync == stomp::SyncState::Pending ||
        traceRead_ == traceWrite_ || Serial.availableForWrite() < 128) return;
    const MidiTrace &trace = traces_[traceRead_ % 20];
    char line[160];
    int used = snprintf(line, sizeof(line), "%s RX len=%u first=",
                        trace.nameFrame ? "NAME" : "SYNC",
                        static_cast<unsigned>(trace.length));
    const size_t firstCount = trace.length < sizeof(trace.first)
                                  ? trace.length : sizeof(trace.first);
    for (size_t i = 0; i < firstCount && used < int(sizeof(line)); ++i)
      used += snprintf(line + used, sizeof(line) - used, "%02X", trace.first[i]);
    if (trace.length > firstCount)
      used += snprintf(line + used, sizeof(line) - used, " tail=");
    if (trace.length > firstCount)
      for (uint8_t byte : trace.tail)
        used += snprintf(line + used, sizeof(line) - used, "%02X", byte);
    if (trace.length >= 39) {
      used += snprintf(line + used, sizeof(line) - used, " bitmap=");
      for (uint8_t byte : trace.bitmap)
        used += snprintf(line + used, sizeof(line) - used, "%02X", byte);
    }
    Serial.println(line);
    ++traceRead_;
  }

  static void received(const uint8_t *bytes, size_t length, void *context) {
    auto &adapter = *static_cast<UsbApplicationAdapter *>(context);
    if (length == 0 || length > stomp::kMaxMidiMessage) {
      adapter.markLost(adapter.activeGeneration(), 2);
      return;
    }
    PlatformEvent event;
    event.kind = PlatformEventKind::Midi;
    event.generation = adapter.activeGeneration();
    if (event.generation == 0) return;
    event.length = static_cast<uint16_t>(length);
    memcpy(event.bytes, bytes, length);
    adapter.enqueue(event);
  }

  static void status(UsbMidiTransport::Event type, bool success,
                     void *context) {
    auto &adapter = *static_cast<UsbApplicationAdapter *>(context);
    PlatformEvent event;
    event.kind = type == UsbMidiTransport::Event::TransmitComplete
                     ? PlatformEventKind::SendComplete
                     : PlatformEventKind::ReceiveLost;
    event.generation = type == UsbMidiTransport::Event::TransmitComplete
                           ? adapter.port_.takeCompletionGeneration()
                           : adapter.activeGeneration();
    if (event.generation == 0) return;
    event.success = success;
    adapter.enqueue(event);
  }

  void enqueue(const PlatformEvent &event) {
    // The GP-5 sends its patch list as a burst. Give the application task a
    // chance to drain the queue instead of declaring receive loss immediately.
    if (xQueueSend(events_, &event, pdMS_TO_TICKS(25)) != pdTRUE)
      markLost(event.generation, 1);
  }

  void markLost(stomp::SessionGeneration generation, uint8_t reason) {
    if (generation == 0 || generation != activeGeneration()) return;
    portENTER_CRITICAL(&mux_);
    lostGeneration_ = generation;
    lostReason_ = reason;
    portEXIT_CRITICAL(&mux_);
  }

  void activeGeneration(stomp::SessionGeneration value) {
    portENTER_CRITICAL(&mux_);
    activeGeneration_ = value;
    portEXIT_CRITICAL(&mux_);
  }

  stomp::SessionGeneration activeGeneration() {
    portENTER_CRITICAL(&mux_);
    const auto value = activeGeneration_;
    portEXIT_CRITICAL(&mux_);
    return value;
  }

  UsbMidiTransport &transport_;
  UsbSendPort &port_;
  stomp::Application &application_;
  QueueHandle_t events_ = nullptr;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  stomp::SessionGeneration activeGeneration_ = 0;
  stomp::SessionGeneration lostGeneration_ = 0;
  uint8_t lostReason_ = 0;
  bool attached_ = false;
  uint16_t vendorId_ = 0;
  uint16_t productId_ = 0;
  MidiTrace traces_[20] = {};
  uint8_t tracesSeen_ = 0;
  uint8_t traceRead_ = 0;
  uint8_t traceWrite_ = 0;
  uint32_t receivedFrames_ = 0;
  uint32_t sendCompletions_ = 0;
};

UsbMidiTransport transport;
UsbSendPort port(transport);
PlatformClock clock;
stomp::Application application(clock, port);
UsbApplicationAdapter adapter(transport, port, application);
stomp::Switching switching(stomp::GestureConfig{});
uint32_t reportedSyncRevision = 0;
uint32_t lastSyncHeartbeatAt = 0;
stomp::SessionGeneration reportedModelGeneration = 0;
uint32_t reportedModelIds[3] = {};
bool reportedModels[3] = {};
stomp::SessionGeneration reportedNamesGeneration = 0;
uint16_t nextNameToPrint = 100;
uint32_t lastNamePrintAt = 0;

const char *resultName(stomp::Result result) {
  switch (result) {
  case stomp::Result::Accepted: return "accepted";
  case stomp::Result::Unsupported: return "unsupported";
  case stomp::Result::NotReady: return "not-ready";
  case stomp::Result::QueueFull: return "queue-full";
  case stomp::Result::TransportFailed: return "transport-failed";
  case stomp::Result::TimedOut: return "timed-out";
  case stomp::Result::Invalid: return "invalid-response";
  case stomp::Result::Interrupted: return "interrupted-by-live-command";
  }
  return "unknown";
}

void reportSync() {
  const stomp::ReadSnapshot &state = application.snapshot();
  if (state.syncRevision == reportedSyncRevision) return;
  reportedSyncRevision = state.syncRevision;
  if (state.sync == stomp::SyncState::Idle) return;
  const unsigned long generation = static_cast<unsigned long>(state.generation);
  if (state.sync == stomp::SyncState::Pending) {
    Serial.printf("SYNC gen=%lu pending\n", generation);
  } else if (state.sync == stomp::SyncState::Interrupted) {
    Serial.printf("SYNC gen=%lu interrupted by live command\n", generation);
  } else if (state.sync == stomp::SyncState::Failed) {
    Serial.printf("SYNC gen=%lu failed reason=%s droppedMidi=%lu droppedControl=%lu\n",
                  generation, resultName(state.syncResult),
                  static_cast<unsigned long>(application.droppedMidi()),
                  static_cast<unsigned long>(application.droppedControls()));
  } else {
    Serial.printf("SYNC gen=%lu confirmed preset=%u",
                  generation, static_cast<unsigned>(state.preset.index));
    for (uint8_t i = 0; i < state.capabilities.blockCount; ++i)
      Serial.printf(" block%u=%u", static_cast<unsigned>(state.blocks[i].id),
                    state.blocks[i].enabled.value ? 1u : 0u);
    Serial.println();
    adapter.resetTrace();
  }
}

void reportSyncHeartbeat() {
  const uint32_t now = millis();
  if (application.snapshot().sync != stomp::SyncState::Pending ||
      uint32_t(now - lastSyncHeartbeatAt) < 1000) return;
  lastSyncHeartbeatAt = now;
  Serial.printf("SYNC WAIT ms=%lu txDone=%lu rx=%lu outBusy=%u outQueued=%u ingress=%u\n",
                static_cast<unsigned long>(now),
                static_cast<unsigned long>(adapter.sendCompletions()),
                static_cast<unsigned long>(adapter.receivedFrames()),
                application.outputBusy() ? 1u : 0u,
                static_cast<unsigned>(application.outputQueued()),
                static_cast<unsigned>(adapter.pendingEvents()));
}

void reportNamesAndModels() {
  const stomp::ReadSnapshot &state = application.snapshot();
  if (state.generation != reportedModelGeneration) {
    reportedModelGeneration = state.generation;
    for (bool &reported : reportedModels) reported = false;
  }
  for (uint8_t i = 0; i < state.capabilities.blockCount; ++i) {
    const auto &block = state.blocks[i];
    if (block.modelCertainty != stomp::Certainty::Confirmed || i >= 3 ||
        (reportedModels[i] && reportedModelIds[i] == block.modelId)) continue;
    reportedModels[i] = true;
    reportedModelIds[i] = block.modelId;
    Serial.printf("EFFECT block%u model=%s id=%08lX\n",
                  static_cast<unsigned>(block.id),
                  block.modelName[0] ? block.modelName : "unknown",
                  static_cast<unsigned long>(block.modelId));
  }
  if (!state.presetNamesComplete) return;
  if (reportedNamesGeneration != state.generation) {
    reportedNamesGeneration = state.generation;
    nextNameToPrint = 0;
    Serial.printf("PATCH NAMES gen=%lu count=%u\n",
                  static_cast<unsigned long>(state.generation),
                  static_cast<unsigned>(state.capabilities.presetCount));
  }
  if (nextNameToPrint >= state.capabilities.presetCount ||
      Serial.availableForWrite() < 64 ||
      uint32_t(millis() - lastNamePrintAt) < 10) return;
  lastNamePrintAt = millis();
  Serial.printf("PATCH P%02u %s\n",
                static_cast<unsigned>(nextNameToPrint),
                state.presetNames[nextNameToPrint]);
  ++nextNameToPrint;
}

void pollSwitches() {
  const uint32_t now = millis();
  for (size_t id = 0; id < BoardPins::kSwitchCount; ++id)
    switching.sample(static_cast<uint8_t>(id),
                     digitalRead(BoardPins::kSwitches[id]) == LOW, now);
  switching.tick(now);
  stomp::GestureEvent event;
  while (switching.pop(event)) application.gesture(event);
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  static_assert(BoardPins::kSwitchCount == stomp::kSwitchCount);
  const uint32_t now = millis();
  for (size_t id = 0; id < BoardPins::kSwitchCount; ++id) {
    pinMode(BoardPins::kSwitches[id], INPUT_PULLUP);
    switching.sample(static_cast<uint8_t>(id),
                     digitalRead(BoardPins::kSwitches[id]) == LOW, now);
  }
  Serial.println("StompBridge replacement USB diagnostic");
  transport.setVerboseTraffic(false);
  if (!adapter.begin()) Serial.println("Replacement USB adapter startup failed");
}

void loop() {
  pollSwitches();
  adapter.poll();
  reportSync();
  reportSyncHeartbeat();
  reportNamesAndModels();
  delay(1);
}
