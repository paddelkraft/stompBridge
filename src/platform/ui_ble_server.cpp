#include "ui_ble_server.h"

#include "nimble_owner.h"
#include "ui/live_json.h"

#include <cstdio>
#include <cstring>

namespace stomp::platform {
namespace {
constexpr char kServiceUuid[] = "9e0d0001-7f4a-4b86-9c31-6a8b7d4c2000";
constexpr char kInfoUuid[] = "9e0d0001-7f4a-4b86-9c31-6a8b7d4c2001";
constexpr char kCommandUuid[] = "9e0d0001-7f4a-4b86-9c31-6a8b7d4c2002";
constexpr char kEventUuid[] = "9e0d0001-7f4a-4b86-9c31-6a8b7d4c2003";
constexpr UBaseType_t kEventCapacity = 12;
constexpr uint32_t kAccessWindowMs = 60000;
}

class UiBleServer::ServerCallbacks final : public NimBLEServerCallbacks {
public:
  explicit ServerCallbacks(UiBleServer &owner) : owner_(owner) {}
  void onConnect(NimBLEServer *, NimBLEConnInfo &info) override {
    Event event; event.kind = EventKind::Connect;
    event.handle = info.getConnHandle(); event.mtu = info.getMTU();
    event.encrypted = info.isEncrypted(); event.bonded = info.isBonded();
    owner_.enqueueCallbackEvent(event);
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &info, int) override {
    Event event; event.kind = EventKind::Disconnect;
    event.handle = info.getConnHandle(); owner_.enqueueCallbackEvent(event);
  }
  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    Event event; event.kind = EventKind::Security;
    event.handle = info.getConnHandle(); event.encrypted = info.isEncrypted();
    event.bonded = info.isBonded(); owner_.enqueueCallbackEvent(event);
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo &info) override {
    Event event; event.kind = EventKind::Mtu;
    event.handle = info.getConnHandle(); event.mtu = mtu;
    owner_.enqueueCallbackEvent(event);
  }
private:
  UiBleServer &owner_;
};

class UiBleServer::CharacteristicCallbacks final : public NimBLECharacteristicCallbacks {
public:
  explicit CharacteristicCallbacks(UiBleServer &owner) : owner_(owner) {}
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &info) override {
    const std::string value = characteristic->getValue();
    if (value.empty() || value.size() > ble_ui::kMaxAttPayload) {
      Event invalid; invalid.kind = EventKind::Write; invalid.handle = info.getConnHandle();
      invalid.length = static_cast<uint16_t>(ble_ui::kMaxAttPayload + 1);
      owner_.enqueueCallbackEvent(invalid);
      return;
    }
    Event event; event.kind = EventKind::Write;
    event.handle = info.getConnHandle(); event.mtu = info.getMTU();
    event.length = static_cast<uint16_t>(value.size());
    memcpy(event.bytes, value.data(), value.size());
    owner_.enqueueCallbackEvent(event);
  }
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &info,
                   uint16_t subValue) override {
    Event event; event.kind = EventKind::Subscribe;
    event.handle = info.getConnHandle(); event.enabled = subValue != 0;
    owner_.enqueueCallbackEvent(event);
  }
private:
  UiBleServer &owner_;
};

bool UiBleServer::begin() {
#ifdef STOMPBRIDGE_UI_SERIAL
  events_ = xQueueCreate(kEventCapacity, sizeof(Event));
  Serial.println("UI-SERIAL:READY");
  return events_ != nullptr;
#else
  if (!beginNimble("StompBridge")) return false;
  events_ = xQueueCreate(kEventCapacity, sizeof(Event));
  if (events_ == nullptr) return false;
  server_ = NimBLEDevice::createServer();
  if (server_ == nullptr) return false;
  serverCallbacks_ = new ServerCallbacks(*this);
  characteristicCallbacks_ = new CharacteristicCallbacks(*this);
  server_->setCallbacks(serverCallbacks_, false);
  NimBLEService *service = server_->createService(kServiceUuid);
  if (service == nullptr) return false;
  protocolInfo_ = service->createCharacteristic(
      kInfoUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC, 192);
  command_ = service->createCharacteristic(
      kCommandUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC,
      static_cast<uint16_t>(ble_ui::kMaxAttPayload));
  eventsCharacteristic_ = service->createCharacteristic(
      kEventUuid, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ |
                      NIMBLE_PROPERTY::READ_ENC,
      static_cast<uint16_t>(ble_ui::kMaxAttPayload));
  if (!protocolInfo_ || !command_ || !eventsCharacteristic_) return false;
  command_->setCallbacks(characteristicCallbacks_);
  eventsCharacteristic_->setCallbacks(characteristicCallbacks_);
  if (!server_->start()) return false;
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setName("StompBridge UI");
  advertising->start();
  refreshProtocolInfo(0);
  return true;
#endif
}

void UiBleServer::toggleAuthorizationWindow() {
  const uint32_t now = millis();
  if (accessWindowOpen()) {
    accessUntil_ = 0;
    closeConnection(true);
    refreshProtocolInfo(pedalGeneration_);
    Serial.println("BLE UI access revoked");
    return;
  }
  accessUntil_ = now + kAccessWindowMs;
  refreshProtocolInfo(pedalGeneration_);
  NimBLEDevice::startAdvertising();
  Serial.println("BLE UI writable window open for 60 seconds");
}

bool UiBleServer::accessWindowOpen() const {
  return accessUntil_ != 0 && static_cast<int32_t>(accessUntil_ - millis()) > 0;
}

bool UiBleServer::enqueueCallbackEvent(const Event &event) {
  if (events_ == nullptr || xQueueSend(events_, &event, 0) != pdTRUE) {
    droppedEvents_.fetch_add(1);
    overflowHandle_.store(event.handle);
    return false;
  }
  return true;
}

void UiBleServer::refreshProtocolInfo(SessionGeneration generation) {
  char info[192];
  snprintf(info, sizeof(info),
           "{\"v\":1,\"epoch\":%lu,\"generation\":%lu,\"maxCommand\":%u,\"maxSnapshot\":%u,\"window\":%s}",
           static_cast<unsigned long>(epoch_),
           static_cast<unsigned long>(generation),
           static_cast<unsigned>(ui::kMaxJsonCommandBytes),
           static_cast<unsigned>(ui::kMaxJsonSnapshotBytes),
           accessWindowOpen() ? "true" : "false");
#ifdef STOMPBRIDGE_UI_SERIAL
  if (serialDevelopmentConnected_)
    Serial.printf("UI-SERIAL:INFO:%s\n", info);
#endif
  if (!protocolInfo_) return;
  protocolInfo_->setValue(info);
}

void UiBleServer::closeConnection(bool restartAdvertising) {
  if (connected_ && server_) server_->disconnect(connectionHandle_);
  connected_ = false; encrypted_ = false; bonded_ = false; subscribed_ = false;
  negotiated_ = false; freshSnapshotRequested_ = false;
  connectionHandle_ = BLE_HS_CONN_HANDLE_NONE;
  reassembler_.reset();
  ingress_.disconnect(epoch_);
  for (auto &pending : pending_) pending = {};
  outbound_.active = false;
  resultQueue_.clear();
  controlQueue_.clear();
  resultGapPending_ = false;
  if (restartAdvertising) {
#ifndef STOMPBRIDGE_UI_SERIAL
    NimBLEDevice::startAdvertising();
#endif
  }
}

void UiBleServer::handleAuthorizationExpiry(uint32_t nowMs) {
  if (accessUntil_ == 0 || static_cast<int32_t>(accessUntil_ - nowMs) > 0) return;
  accessUntil_ = 0;
  closeConnection(false);
  refreshProtocolInfo(pedalGeneration_);
  Serial.println("BLE UI access window expired");
}

void UiBleServer::prepareSnapshot(const ReadSnapshot &snapshot, const char *deviceId) {
  if (!subscribed_ || !negotiated_ || outbound_.active) {
    freshSnapshotRequested_ = true;
    return;
  }
  const size_t length = ui::encodeSnapshot(snapshot, epoch_, connected_,
                                            reinterpret_cast<char *>(outbound_.bytes),
                                            sizeof(outbound_.bytes), deviceId);
  if (length == 0) {
#ifdef STOMPBRIDGE_UI_SERIAL
    if (serialDevelopmentConnected_ && !serialSnapshotDiagnosticReported_) {
      Serial.println("UI-SERIAL:DEBUG:snapshot encode failed");
      serialSnapshotDiagnosticReported_ = true;
    }
#endif
    noteLostResult(); return;
  }
  outbound_.active = true; outbound_.type = ble_ui::MessageType::Snapshot;
  outbound_.length = static_cast<uint16_t>(length); outbound_.index = 0;
  outbound_.transfer = static_cast<uint16_t>(++nextTransfer_);
  outbound_.startedAt = millis(); observedRevision_ = snapshot.revision;
  freshSnapshotRequested_ = false;
#ifdef STOMPBRIDGE_UI_SERIAL
  if (serialDevelopmentConnected_ && !serialSnapshotDiagnosticReported_) {
    Serial.printf("UI-SERIAL:DEBUG:snapshot encoded bytes=%u device=%s phase=%u\n",
                  static_cast<unsigned>(length), deviceId ? deviceId : "unknown",
                  static_cast<unsigned>(snapshot.connection));
    serialSnapshotDiagnosticReported_ = true;
  }
#endif
}

void UiBleServer::publishResult(const ui::RequestResult &result) {
  results_.record(result);
  if (!resultQueue_.push(result)) noteLostResult();
}

void UiBleServer::noteLostResult() {
  ++lostResults_;
  resultGapPending_ = true;
}

void UiBleServer::startOutbound(ble_ui::MessageType type, const uint8_t *bytes,
                                size_t length) {
  if (bytes == nullptr || length == 0 || length > ui::kMaxJsonCommandBytes) {
    noteLostResult();
    return;
  }
  PendingText text;
  text.type = type;
  text.length = static_cast<uint16_t>(length);
  memcpy(text.bytes, bytes, length);
  if (!controlQueue_.push(text)) noteLostResult();
}

void UiBleServer::prepareNextOutbound(const ReadSnapshot &snapshot,
                                      const char *deviceId) {
  if (outbound_.active || !connected_ || !encrypted_ || !bonded_ || !negotiated_) return;
  PendingText control;
  if (controlQueue_.pop(control)) {
    outbound_.active = true; outbound_.type = control.type;
    outbound_.length = control.length; outbound_.index = 0;
    outbound_.transfer = static_cast<uint16_t>(++nextTransfer_);
    outbound_.startedAt = millis();
    memcpy(outbound_.bytes, control.bytes, control.length);
    return;
  }
  if (resultGapPending_) {
    PendingText gap;
    gap.type = ble_ui::MessageType::Result;
    const int length = snprintf(reinterpret_cast<char *>(gap.bytes), sizeof(gap.bytes),
        "{\"v\":1,\"kind\":\"error\",\"code\":\"resultGap\",\"lost\":%lu,\"oldest\":%lu}",
        static_cast<unsigned long>(lostResults_),
        static_cast<unsigned long>(results_.oldestRetainedSequence()));
    if (length > 0 && static_cast<size_t>(length) < sizeof(gap.bytes)) {
      gap.length = static_cast<uint16_t>(length);
      if (controlQueue_.push(gap)) resultGapPending_ = false;
    }
    return;
  }
  if (!subscribed_) return;
  ui::RequestResult result;
  if (resultQueue_.pop(result)) {
    const size_t length = ui::encodeResult(result,
        reinterpret_cast<char *>(outbound_.bytes), sizeof(outbound_.bytes));
    if (!length) { noteLostResult(); return; }
    outbound_.active = true; outbound_.type = ble_ui::MessageType::Result;
    outbound_.length = static_cast<uint16_t>(length); outbound_.index = 0;
    outbound_.transfer = static_cast<uint16_t>(++nextTransfer_);
    outbound_.startedAt = millis();
    return;
  }
  if (freshSnapshotRequested_ || snapshot.revision != observedRevision_)
    prepareSnapshot(snapshot, deviceId);
}

void UiBleServer::sendOutboundFragment(uint32_t nowMs) {
#ifdef STOMPBRIDGE_UI_SERIAL
  if (serialDevelopmentConnected_) {
    if (!outbound_.active) return;
    if (static_cast<uint32_t>(nowMs - outbound_.startedAt) > ble_ui::kFragmentTimeoutMs) {
      if (outbound_.type == ble_ui::MessageType::Snapshot) freshSnapshotRequested_ = true;
      else noteLostResult();
      outbound_.active = false;
      return;
    }
    ble_ui::Frame frame;
    const auto status = ble_ui::encodeFragment(
        outbound_.type, outbound_.transfer, outbound_.bytes, outbound_.length,
        outbound_.index, 20, frame);
    if (status != ble_ui::FrameError::None) { outbound_.active = false; noteLostResult(); return; }
    if (!sendSerialDevelopmentFrame(frame.bytes, frame.length)) return;
    outbound_.startedAt = nowMs;
    ++outbound_.index;
    if (outbound_.index >= ble_ui::fragmentCount(outbound_.length, 20)) {
      if (outbound_.type == ble_ui::MessageType::Snapshot &&
          !serialSnapshotTransmittedReported_) {
        Serial.printf("UI-SERIAL:DEBUG:snapshot transmitted bytes=%u\n",
                      static_cast<unsigned>(outbound_.length));
        serialSnapshotTransmittedReported_ = true;
      }
      outbound_.active = false;
    }
    return;
  }
#endif
  if (!outbound_.active || !eventsCharacteristic_) return;
  if (static_cast<uint32_t>(nowMs - outbound_.startedAt) > ble_ui::kFragmentTimeoutMs) {
    if (outbound_.type == ble_ui::MessageType::Snapshot) freshSnapshotRequested_ = true;
    else noteLostResult();
    outbound_.active = false;
    return;
  }
  ble_ui::Frame frame;
  const auto status = ble_ui::encodeFragment(
      outbound_.type, outbound_.transfer, outbound_.bytes, outbound_.length,
      outbound_.index, attPayload_, frame);
  if (status != ble_ui::FrameError::None) { outbound_.active = false; noteLostResult(); return; }
  if (!eventsCharacteristic_->notify(frame.bytes, frame.length, connectionHandle_)) return;
  outbound_.startedAt = nowMs;
  ++outbound_.index;
  if (outbound_.index >= ble_ui::fragmentCount(outbound_.length, attPayload_))
    outbound_.active = false;
}

void UiBleServer::handleMessage(const ble_ui::Message &message, Application &application) {
  const uint8_t *bytes = message.bytes;
  if (message.type == ble_ui::MessageType::Hello) {
    ui::HelloRequest hello;
    const auto status = ui::decodeHello(bytes, message.length, hello);
    if (status != ui::JsonStatus::Ok || hello.epoch != epoch_) {
      const char error[] = "{\"v\":1,\"kind\":\"error\",\"code\":\"versionMismatch\"}";
      if (status == ui::JsonStatus::VersionMismatch || status == ui::JsonStatus::Unsupported)
        startOutbound(ble_ui::MessageType::Result, reinterpret_cast<const uint8_t *>(error), sizeof(error)-1);
      return;
    }
    negotiated_ = true;
    char reply[96];
    snprintf(reply, sizeof(reply), "{\"v\":1,\"kind\":\"helloResult\",\"selected\":1,\"epoch\":%lu}", static_cast<unsigned long>(epoch_));
    startOutbound(ble_ui::MessageType::Result, reinterpret_cast<const uint8_t *>(reply), strlen(reply));
    return;
  }
  if (!negotiated_ || !subscribed_) return;
  if (message.type != ble_ui::MessageType::Command && message.type != ble_ui::MessageType::Resync) return;
  ui::UiRequest request;
  const auto status = ui::decodeRequest(bytes, message.length, request);
  if (status != ui::JsonStatus::Ok) {
#ifdef STOMPBRIDGE_UI_SERIAL
    if (serialDevelopmentConnected_ && !serialSnapshotDiagnosticReported_)
      Serial.printf("UI-SERIAL:DEBUG:request decode failed status=%u type=%u\n",
                    static_cast<unsigned>(status),
                    static_cast<unsigned>(message.type));
#endif
    if (status == ui::JsonStatus::VersionMismatch) {
      const char error[] = "{\"v\":1,\"kind\":\"error\",\"code\":\"versionMismatch\"}";
      startOutbound(ble_ui::MessageType::Result, reinterpret_cast<const uint8_t *>(error), sizeof(error)-1);
    }
    return;
  }
  if (request.epoch != epoch_) {
    publishResult({request.requestId, epoch_, application.generation(), 0,
                   OperationPhase::Failed, Result::Interrupted, Result::Interrupted});
    return;
  }
  if (request.requestId <= lastRequestId_) {
    publishResult({request.requestId, epoch_, application.generation(), 0,
                   OperationPhase::Failed, Result::Invalid, Result::Invalid});
    return;
  }
  lastRequestId_ = request.requestId;
  if (request.generation != application.generation()) {
    publishResult({request.requestId, epoch_, application.generation(), 0,
                   OperationPhase::Failed, Result::Interrupted, Result::Interrupted});
    return;
  }
  if (request.snapshot) {
#ifdef STOMPBRIDGE_UI_SERIAL
    if (serialDevelopmentConnected_ && !serialSnapshotDiagnosticReported_)
      Serial.printf("UI-SERIAL:DEBUG:snapshot request accepted id=%lu\n",
                    static_cast<unsigned long>(request.requestId));
#endif
    freshSnapshotRequested_ = true;
    return;
  }
  const Result queued = ingress_.enqueue(request.requestId, request.epoch,
      request.generation, request.command, request.target, request.enabled);
#ifdef STOMPBRIDGE_UI_SERIAL
  Serial.printf("UI-SERIAL:DEBUG:command queued id=%lu kind=%u target=%u enabled=%u result=%u\n",
                static_cast<unsigned long>(request.requestId),
                static_cast<unsigned>(request.command),
                static_cast<unsigned>(request.target), request.enabled ? 1u : 0u,
                static_cast<unsigned>(queued));
#endif
  if (queued != Result::Accepted)
    publishResult({request.requestId, epoch_, request.generation, 0,
                   OperationPhase::Failed, queued, queued});
}

#ifdef STOMPBRIDGE_UI_SERIAL
void UiBleServer::handleSerialDevelopmentLine(const char *line, size_t length,
                                              Application &application) {
  constexpr char kOpen[] = "UI-SERIAL:OPEN:";
  constexpr char kClose[] = "UI-SERIAL:CLOSE:";
  constexpr char kFrame[] = "UI-SERIAL:FRAME:";
  const auto nibble = [](char value) -> int {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  };
  if (length == sizeof(kOpen) - 1 + 8 &&
      memcmp(line, kOpen, sizeof(kOpen) - 1) == 0) {
    uint32_t token = 0;
    for (size_t i = sizeof(kOpen) - 1; i < length; ++i) {
      const int digit = nibble(line[i]);
      if (digit < 0) return;
      token = (token << 4) | static_cast<uint32_t>(digit);
    }
    if (serialDevelopmentConnected_ && token == serialSessionToken_) {
      refreshProtocolInfo(application.generation());
      return;
    }
    if (serialDevelopmentConnected_) closeConnection(false);
    serialDevelopmentConnected_ = true;
    serialSessionToken_ = token;
    connected_ = encrypted_ = bonded_ = subscribed_ = true;
    negotiated_ = false;
    serialSnapshotDiagnosticReported_ = false;
    serialSnapshotTransmittedReported_ = false;
    ++epoch_;
    if (epoch_ == 0) ++epoch_;
    lastRequestId_ = 0;
    attPayload_ = 20;
    ingress_.connect(epoch_, application.generation());
    refreshProtocolInfo(application.generation());
    Serial.println("UI-SERIAL:OPENED");
    return;
  }
  if (length == sizeof(kClose) - 1 + 8 &&
      memcmp(line, kClose, sizeof(kClose) - 1) == 0) {
    uint32_t token = 0;
    for (size_t i = sizeof(kClose) - 1; i < length; ++i) {
      const int digit = nibble(line[i]);
      if (digit < 0) return;
      token = (token << 4) | static_cast<uint32_t>(digit);
    }
    if (token != serialSessionToken_) return;
    serialDevelopmentConnected_ = false;
    serialSessionToken_ = 0;
    closeConnection(false);
    Serial.println("UI-SERIAL:CLOSED");
    return;
  }
  if (length < sizeof(kFrame) - 1 ||
      memcmp(line, kFrame, sizeof(kFrame) - 1) != 0 || !serialDevelopmentConnected_)
    return;
  const char *hex = line + sizeof(kFrame) - 1;
  const size_t hexLength = length - (sizeof(kFrame) - 1);
  if (hexLength == 0 || (hexLength & 1u) != 0 || hexLength / 2 > ble_ui::kMaxAttPayload) {
    Serial.printf("UI-SERIAL:DEBUG:frame rejected hex length=%u\n",
                  static_cast<unsigned>(hexLength));
    return;
  }
  uint8_t frame[ble_ui::kMaxAttPayload];
  for (size_t i = 0; i < hexLength; i += 2) {
    const int high = nibble(hex[i]);
    const int low = nibble(hex[i + 1]);
    if (high < 0 || low < 0) {
      Serial.printf("UI-SERIAL:DEBUG:frame rejected invalid hex offset=%u\n",
                    static_cast<unsigned>(i));
      return;
    }
    frame[i / 2] = static_cast<uint8_t>((high << 4) | low);
  }
  const auto status = reassembler_.accept(frame, hexLength / 2,
      ble_ui::kMaxAttPayload, millis(), inbound_);
  if (status == ble_ui::FrameError::Complete) {
    Serial.printf("UI-SERIAL:DEBUG:frame received type=%u bytes=%u\n",
                  static_cast<unsigned>(inbound_.type),
                  static_cast<unsigned>(inbound_.length));
    handleMessage(inbound_, application);
  }
  else if (status == ble_ui::FrameError::Malformed ||
           status == ble_ui::FrameError::VersionMismatch ||
           status == ble_ui::FrameError::OutOfOrder ||
           status == ble_ui::FrameError::Duplicate ||
           status == ble_ui::FrameError::TooLarge ||
           status == ble_ui::FrameError::Timeout) {
    const char error[] = "{\"v\":1,\"kind\":\"error\",\"code\":\"badFrame\"}";
    startOutbound(ble_ui::MessageType::Result,
                  reinterpret_cast<const uint8_t *>(error), sizeof(error) - 1);
  }
}

void UiBleServer::pollSerialDevelopmentLink(Application &application) {
  while (Serial.available() > 0) {
    const int byte = Serial.read();
    if (byte < 0) break;
    if (byte == '\n') {
      if (!serialLineOverflow_ && serialLineLength_ > 0) {
        if (serialLine_[serialLineLength_ - 1] == '\r') --serialLineLength_;
        handleSerialDevelopmentLine(serialLine_, serialLineLength_, application);
      }
      serialLineLength_ = 0;
      serialLineOverflow_ = false;
    } else if (!serialLineOverflow_) {
      if (serialLineLength_ + 1 >= sizeof(serialLine_)) serialLineOverflow_ = true;
      else serialLine_[serialLineLength_++] = static_cast<char>(byte);
    }
  }
}

bool UiBleServer::sendSerialDevelopmentFrame(const uint8_t *bytes, size_t length) {
  if (bytes == nullptr || length == 0 || length > ble_ui::kMaxAttPayload) return false;
  static constexpr char kHex[] = "0123456789abcdef";
  Serial.print("UI-SERIAL:FRAME:");
  for (size_t i = 0; i < length; ++i) {
    Serial.write(kHex[bytes[i] >> 4]);
    Serial.write(kHex[bytes[i] & 0x0f]);
  }
  Serial.write('\n');
  return true;
}
#endif

void UiBleServer::poll(Application &application) {
  application.setObserver(this);
#ifdef STOMPBRIDGE_UI_SERIAL
  pollSerialDevelopmentLink(application);
#endif
  handleAuthorizationExpiry(millis());
  const uint16_t overflowHandle = overflowHandle_.exchange(BLE_HS_CONN_HANDLE_NONE);
  if (overflowHandle != BLE_HS_CONN_HANDLE_NONE) {
    accessUntil_ = 0;
    if (connected_ && connectionHandle_ == overflowHandle) closeConnection(true);
    else if (server_) server_->disconnect(overflowHandle);
    refreshProtocolInfo(application.generation());
    Serial.println("BLE UI callback queue overflow; connection closed");
  }
  if (pedalGeneration_ != application.generation()) {
    const SessionGeneration previous = pedalGeneration_;
    pedalGeneration_ = application.generation();
    ingress_.pedalGeneration(pedalGeneration_);
    for (auto &pending : pending_) {
      if (pending.active && pending.generation == previous) {
        publishResult({pending.request, pending.epoch, pending.generation,
                       pending.operation, OperationPhase::Failed,
                       Result::Interrupted, Result::Accepted});
        pending = {};
      }
    }
    refreshProtocolInfo(pedalGeneration_);
  }
  if (events_) {
    Event event;
    while (xQueueReceive(events_, &event, 0) == pdTRUE) {
      if (event.kind == EventKind::Connect) {
        if (!accessWindowOpen() || connected_) { server_->disconnect(event.handle); continue; }
        connected_ = true; connectionHandle_ = event.handle;
        ++epoch_; if (epoch_ == 0) ++epoch_;
        lastRequestId_ = 0;
        encrypted_ = event.encrypted; bonded_ = event.bonded;
        attPayload_ = event.mtu > 3 ? event.mtu - 3 : 20;
        if (attPayload_ > ble_ui::kMaxAttPayload) attPayload_ = ble_ui::kMaxAttPayload;
        ingress_.connect(epoch_, application.generation());
        refreshProtocolInfo(application.generation());
        if (!encrypted_) NimBLEDevice::startSecurity(connectionHandle_);
      } else if (event.handle == connectionHandle_) {
        if (event.kind == EventKind::Disconnect) {
          accessUntil_ = 0;
          closeConnection(true);
        } else if (event.kind == EventKind::Security) {
          encrypted_ = event.encrypted; bonded_ = event.bonded;
          if (!encrypted_ || !bonded_) {
            accessUntil_ = 0;
            closeConnection(false);
            refreshProtocolInfo(pedalGeneration_);
          }
        } else if (event.kind == EventKind::Subscribe) {
          subscribed_ = event.enabled;
        } else if (event.kind == EventKind::Mtu) {
          attPayload_ = event.mtu > 3 ? event.mtu - 3 : 20;
          if (attPayload_ > ble_ui::kMaxAttPayload) attPayload_ = ble_ui::kMaxAttPayload;
        } else if (event.kind == EventKind::Write) {
          if (!encrypted_ || !bonded_ || event.length == 0 || event.mtu < 23 || event.length > event.mtu - 3) continue;
          const auto frameStatus = reassembler_.accept(event.bytes, event.length,
                                                       event.mtu - 3, millis(), inbound_);
          if (frameStatus == ble_ui::FrameError::Complete) handleMessage(inbound_, application);
          else if (frameStatus == ble_ui::FrameError::Malformed ||
                   frameStatus == ble_ui::FrameError::VersionMismatch ||
                   frameStatus == ble_ui::FrameError::OutOfOrder ||
                   frameStatus == ble_ui::FrameError::Duplicate ||
                   frameStatus == ble_ui::FrameError::TooLarge ||
                   frameStatus == ble_ui::FrameError::Timeout) {
            const char error[] = "{\"v\":1,\"kind\":\"error\",\"code\":\"badFrame\"}";
            startOutbound(ble_ui::MessageType::Result, reinterpret_cast<const uint8_t *>(error), sizeof(error)-1);
          }
        }
      }
    }
  }
  ui::CommandRequest request;
  while (connected_ && encrypted_ && bonded_ && negotiated_ &&
         ingress_.pop(request)) {
    PendingOperation *slot = nullptr;
    for (auto &candidate : pending_)
      if (!candidate.active) { slot = &candidate; break; }
    if (slot == nullptr) {
      publishResult({request.requestId, request.uiEpoch, request.generation, 0,
                     OperationPhase::Failed, Result::QueueFull,
                     Result::QueueFull});
      continue;
    }
    const Admission admission = application.submit(request.command.kind,
        request.command.target, request.command.enabled);
#ifdef STOMPBRIDGE_UI_SERIAL
    Serial.printf("UI-SERIAL:DEBUG:command admission id=%lu result=%u operation=%lu\n",
                  static_cast<unsigned long>(request.requestId),
                  static_cast<unsigned>(admission.result),
                  static_cast<unsigned long>(admission.operation));
#endif
    if (admission.result == Result::Accepted)
      *slot = {true, request.requestId, request.uiEpoch,
               request.generation, admission.operation};
    const bool accepted = admission.result == Result::Accepted;
    publishResult({request.requestId, request.uiEpoch, request.generation,
        admission.operation, accepted ? OperationPhase::Queued : OperationPhase::Failed,
        admission.result, admission.result});
  }
  if (connected_ && encrypted_ && bonded_ && negotiated_ && subscribed_) {
    prepareNextOutbound(application.snapshot(), application.deviceId());
    sendOutboundFragment(millis());
  }
}

void UiBleServer::operationChanged(OperationId operation,
                                   SessionGeneration generation,
                                   OperationPhase phase, Result result) {
  if (generation != pedalGeneration_) return;
  for (auto &pending : pending_) {
    if (!pending.active || pending.operation != operation ||
        pending.generation != generation) continue;
    publishResult({pending.request, pending.epoch, pending.generation,
                   pending.operation, phase, result, Result::Accepted});
    if (phase == OperationPhase::Failed ||
        phase == OperationPhase::TransportComplete ||
        phase == OperationPhase::Observed)
      pending = {};
    return;
  }
}

} // namespace stomp::platform
