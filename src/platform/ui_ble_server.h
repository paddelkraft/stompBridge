#pragma once

#include "core/application.h"
#include "platform/ui_ble_frames.h"
#include "ui/live_ingress.h"
#include "ui/live_json.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>

namespace stomp::platform {

class UiBleServer final : public ApplicationObserver {
public:
  bool begin();
  void poll(Application &application);
  // The utility switch long-hold toggles this window. A revoke disconnects
  // the screen and invalidates its epoch immediately.
  void toggleAuthorizationWindow();
  bool accessWindowOpen() const;
  bool screenConnected() const { return connected_; }
  uint32_t droppedCallbackEvents() const { return droppedEvents_.load(); }
  void operationChanged(OperationId operation, SessionGeneration generation,
                        OperationPhase phase, Result result) override;

private:
  class ServerCallbacks;
  class CharacteristicCallbacks;
  enum class EventKind : uint8_t { Connect, Disconnect, Security, Subscribe, Mtu, Write };
  struct Event {
    EventKind kind = EventKind::Write;
    uint16_t handle = 0;
    uint16_t mtu = 23;
    uint16_t length = 0;
    bool enabled = false;
    bool encrypted = false;
    bool bonded = false;
    uint8_t bytes[ble_ui::kMaxAttPayload] = {};
  };
  struct PendingOperation {
    bool active = false;
    uint32_t request = 0;
    uint32_t epoch = 0;
    SessionGeneration generation = 0;
    OperationId operation = 0;
  };
  struct Outbound {
    bool active = false;
    ble_ui::MessageType type = ble_ui::MessageType::Result;
    uint16_t transfer = 0;
    uint16_t length = 0;
    uint16_t index = 0;
    uint32_t startedAt = 0;
    uint8_t bytes[ble_ui::kMaxMessage + 1] = {};
  };
  struct PendingText {
    ble_ui::MessageType type = ble_ui::MessageType::Result;
    uint16_t length = 0;
    uint8_t bytes[ui::kMaxJsonCommandBytes] = {};
  };

  bool enqueueCallbackEvent(const Event &event);
  void handleMessage(const ble_ui::Message &message, Application &application);
  void publishResult(const ui::RequestResult &result);
  void noteLostResult();
  void startOutbound(ble_ui::MessageType type, const uint8_t *bytes, size_t length);
  void prepareSnapshot(const ReadSnapshot &snapshot, const char *deviceId);
  void prepareNextOutbound(const ReadSnapshot &snapshot, const char *deviceId);
  void sendOutboundFragment(uint32_t nowMs);
  void refreshProtocolInfo(SessionGeneration generation);
  void closeConnection();
  void handleAuthorizationExpiry(uint32_t nowMs);
#ifndef STOMPBRIDGE_UI_SERIAL
  void ensureAdvertising(bool restart = false);
#endif
#ifdef STOMPBRIDGE_UI_SERIAL
  void pollSerialDevelopmentLink(Application &application);
  void handleSerialDevelopmentLine(const char *line, size_t length,
                                   Application &application);
  bool sendSerialDevelopmentFrame(const uint8_t *bytes, size_t length);
#endif

  QueueHandle_t events_ = nullptr;
  NimBLEServer *server_ = nullptr;
  ServerCallbacks *serverCallbacks_ = nullptr;
  CharacteristicCallbacks *characteristicCallbacks_ = nullptr;
  NimBLECharacteristic *protocolInfo_ = nullptr;
  NimBLECharacteristic *command_ = nullptr;
  NimBLECharacteristic *eventsCharacteristic_ = nullptr;
  ble_ui::Reassembler reassembler_;
  ble_ui::Message inbound_;
  ui::LiveIngress ingress_;
  ui::ResultLedger results_;
  BoundedQueue<ui::RequestResult, ui::kResultHistoryCapacity> resultQueue_;
  BoundedQueue<PendingText, 4> controlQueue_;
  // The connection manager can retain 16 commands plus one command that the
  // application task has dequeued but whose plugin operation is still active.
  PendingOperation pending_[kApplicationCommandQueueCapacity + 1] = {};
  Outbound outbound_;
  uint32_t accessUntil_ = 0;
  uint32_t epoch_ = 0;
  uint32_t lastRequestId_ = 0;
  uint32_t nextTransfer_ = 0;
  std::atomic<uint32_t> droppedEvents_{0};
  std::atomic<uint16_t> overflowHandle_{BLE_HS_CONN_HANDLE_NONE};
  uint32_t lostResults_ = 0;
  bool resultGapPending_ = false;
  uint32_t observedRevision_ = 0;
#ifndef STOMPBRIDGE_UI_SERIAL
  NimBLEAdvertising *advertising_ = nullptr;
  uint32_t lastAdvertisingCheck_ = 0;
  uint8_t advertisingFailures_ = 0;
  bool advertisingResetRequested_ = false;
  uint32_t reportedSnapshotNamesRevision_ = 0;
#endif
  uint16_t connectionHandle_ = BLE_HS_CONN_HANDLE_NONE;
  uint16_t attPayload_ = 20;
  SessionGeneration pedalGeneration_ = 0;
  bool connected_ = false;
  bool encrypted_ = false;
  bool bonded_ = false;
  bool subscribed_ = false;
  bool negotiated_ = false;
  bool freshSnapshotRequested_ = false;
#ifdef STOMPBRIDGE_UI_SERIAL
  char serialLine_[1100] = {};
  size_t serialLineLength_ = 0;
  bool serialLineOverflow_ = false;
  bool serialDevelopmentConnected_ = false;
  uint32_t serialSessionToken_ = 0;
  bool serialSnapshotDiagnosticReported_ = false;
  bool serialSnapshotTransmittedReported_ = false;
#endif
};

} // namespace stomp::platform
