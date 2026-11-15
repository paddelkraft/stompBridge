#include "ble_midi_transport.h"

#include <algorithm>
#include <cctype>

namespace {

NimBLEUUID kBleMidiService("03b80e5a-ede8-4b33-a751-6ce34ec4c700");
NimBLEUUID kBleMidiCharacteristic("7772e5db-3868-4112-a1a9-f2669d106bf3");

class MidiAdvertisementCallbacks : public NimBLEScanCallbacks {
public:
  explicit MidiAdvertisementCallbacks(BleMidiTransport &owner) : owner_(owner) {}

  void onResult(const NimBLEAdvertisedDevice *device) override {
    if (device == nullptr) {
      return;
    }
    bool isMidiDevice = device->isAdvertisingService(kBleMidiService);
    if (device->haveName()) {
      const std::string advertisedName = device->getName();
      std::string nameForMatch = advertisedName;
      std::transform(nameForMatch.begin(), nameForMatch.end(),
                     nameForMatch.begin(), [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                     });
      isMidiDevice = isMidiDevice ||
                     nameForMatch.find("sonic master") != std::string::npos ||
                     nameForMatch.find("pocket master") != std::string::npos ||
                     nameForMatch.find("pocketmaster") != std::string::npos;
      Serial.printf("BLE seen: %s [%s], RSSI %d dBm%s\n",
                    advertisedName.c_str(),
                    device->getAddress().toString().c_str(), device->getRSSI(),
                    isMidiDevice ? " (MIDI candidate)" : "");
    }
    if (isMidiDevice) {
      owner_.onAdvertisement(*device);
    }
  }

private:
  BleMidiTransport &owner_;
};

class MidiClientCallbacks : public NimBLEClientCallbacks {
public:
  explicit MidiClientCallbacks(BleMidiTransport &owner) : owner_(owner) {}
  void onDisconnect(NimBLEClient *, int reason) override {
    Serial.printf("BLE-MIDI peer disconnected, reason %d\n", reason);
    owner_.onDisconnected();
  }

  void onConnectFail(NimBLEClient *, int reason) override {
    Serial.printf("BLE-MIDI connection failed, reason %d\n", reason);
  }

private:
  BleMidiTransport &owner_;
};

} // namespace

bool BleMidiTransport::begin() {
  if (initialized_) {
    return true;
  }
  NimBLEDevice::init("StompBridge");
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(false, false, true);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  scan_ = NimBLEDevice::getScan();
  scan_->setScanCallbacks(new MidiAdvertisementCallbacks(*this));
  scan_->setActiveScan(true);
  scan_->setInterval(100);
  scan_->setWindow(80);
  initialized_ = true;
  Serial.println("BLE-MIDI central ready");
  startScan();
  return true;
}

void BleMidiTransport::startScan() {
  if (!initialized_ || connecting_ || connected_ || scan_->isScanning()) {
    return;
  }
  Serial.println("BLE-MIDI scanning for advertised MIDI service...");
  if (!scan_->start(5000, false, true)) {
    Serial.println("BLE-MIDI scan failed to start");
    nextScanAtMs_ = millis() + 2000;
  }
}

void BleMidiTransport::onAdvertisement(const NimBLEAdvertisedDevice &device) {
  portENTER_CRITICAL(&stateMux_);
  if (candidate_ == nullptr && !connected_) {
    candidate_ = new NimBLEAdvertisedDevice(device);
    candidateReadyAtMs_ = millis() + 100;
  }
  portEXIT_CRITICAL(&stateMux_);
  if (scan_ != nullptr) {
    scan_->stop();
  }
}

bool BleMidiTransport::connectCandidate() {
  NimBLEAdvertisedDevice *device = nullptr;
  portENTER_CRITICAL(&stateMux_);
  device = candidate_;
  candidate_ = nullptr;
  connecting_ = device != nullptr;
  portEXIT_CRITICAL(&stateMux_);
  if (device == nullptr) {
    return false;
  }

  const std::string displayName =
      device->haveName() ? device->getName() : "unnamed";
  Serial.printf("BLE-MIDI service found: %s [%s], RSSI %d dBm\n",
                displayName.c_str(), device->getAddress().toString().c_str(),
                device->getRSSI());

  if (client_ == nullptr) {
    client_ = NimBLEDevice::createClient(device->getAddress());
    if (clientCallbacks_ == nullptr) {
      clientCallbacks_ = new MidiClientCallbacks(*this);
    }
    client_->setClientCallbacks(clientCallbacks_, false);
  }
  // Leave enough supervision margin for ESP32 Wi-Fi/BLE coexistence. The old
  // 1.5 s timeout could drop an otherwise healthy pedal connection while the
  // web UI was serving/polling over Wi-Fi.
  client_->setConnectionParams(24, 40, 0, 500);
  client_->setConnectTimeout(5000);

  bool ok = client_->connect(device, true, false, true);
  delete device;
  if (!ok) {
    Serial.printf("BLE-MIDI connection failed (NimBLE error %d)\n",
                  client_->getLastError());
  } else {
    NimBLERemoteService *service = client_->getService(kBleMidiService);
    if (service == nullptr) {
      Serial.println("BLE peer did not expose the advertised MIDI service");
      ok = false;
    } else {
      midiCharacteristic_ = service->getCharacteristic(kBleMidiCharacteristic);
      if (midiCharacteristic_ == nullptr ||
          (!midiCharacteristic_->canWrite() &&
           !midiCharacteristic_->canWriteNoResponse())) {
        Serial.println("BLE peer MIDI characteristic is missing or not writable");
        midiCharacteristic_ = nullptr;
        ok = false;
      } else if (!midiCharacteristic_->canNotify() ||
                 !midiCharacteristic_->subscribe(
                     true,
                     [this](NimBLERemoteCharacteristic *, uint8_t *data,
                            size_t length, bool) {
                       onMidiNotification(data, length);
                     })) {
        Serial.println("BLE peer MIDI notifications could not be enabled");
        ok = false;
      }
    }
  }

  portENTER_CRITICAL(&stateMux_);
  connecting_ = false;
  connected_ = ok;
  portEXIT_CRITICAL(&stateMux_);

  if (!ok) {
    if (client_ != nullptr && client_->isConnected()) {
      client_->disconnect();
    }
    if (client_ != nullptr && !client_->isConnected()) {
      NimBLEDevice::deleteClient(client_);
      client_ = nullptr;
    }
    nextScanAtMs_ = millis() + 2000;
    return false;
  }
  Serial.printf("BLE-MIDI connected to %s\n", displayName.c_str());
  return true;
}

void BleMidiTransport::onMidiNotification(const uint8_t *data, size_t length) {
  if (data == nullptr || length <= 2) {
    return;
  }
  // GP-5 notifications use the same two-byte BLE-MIDI timestamp header as
  // outgoing messages. Device/profile code receives transport-free MIDI.
  deliverReceived(data + 2, length - 2);
}

void BleMidiTransport::onDisconnected() {
  portENTER_CRITICAL(&stateMux_);
  connected_ = false;
  connecting_ = false;
  midiCharacteristic_ = nullptr;
  portEXIT_CRITICAL(&stateMux_);
  nextScanAtMs_ = millis() + 1000;
  Serial.println("BLE-MIDI disconnected");
}

void BleMidiTransport::poll() {
  if (!initialized_) {
    return;
  }
  bool haveCandidate = false;
  uint32_t candidateReadyAtMs = 0;
  portENTER_CRITICAL(&stateMux_);
  haveCandidate = candidate_ != nullptr;
  candidateReadyAtMs = candidateReadyAtMs_;
  portEXIT_CRITICAL(&stateMux_);
  if (haveCandidate) {
    if (!scan_->isScanning() &&
        static_cast<int32_t>(millis() - candidateReadyAtMs) >= 0) {
      connectCandidate();
    }
    return;
  }
  if (!connected_ && !connecting_ && !scan_->isScanning() &&
      static_cast<int32_t>(millis() - nextScanAtMs_) >= 0) {
    nextScanAtMs_ = millis() + 7000;
    startScan();
  }
}

bool BleMidiTransport::connected() const {
  portENTER_CRITICAL(&stateMux_);
  const bool value = connected_;
  portEXIT_CRITICAL(&stateMux_);
  return value;
}

bool BleMidiTransport::send(const uint8_t *midi, size_t length) {
  if (!connected() || midiCharacteristic_ == nullptr || midi == nullptr ||
      length == 0 || length > 64) {
    return false;
  }
  uint8_t packet[66];
  // A zero timestamp is valid BLE-MIDI and matches the exact framing used by
  // the known-working GP-5 implementation.  The GP-5 appears to reject SysEx
  // packets carrying a non-zero timestamp even though they are valid.
  packet[0] = 0x80;
  packet[1] = 0x80;
  memcpy(packet + 2, midi, length);
  const bool withResponse = !midiCharacteristic_->canWriteNoResponse();
  const bool sent = midiCharacteristic_->writeValue(packet, length + 2, withResponse);
  if (!sent) {
    Serial.println("BLE-MIDI characteristic write failed");
  }
  return sent;
}
