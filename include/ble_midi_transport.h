#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "midi_transport.h"

class BleMidiTransport : public MidiTransport {
public:
  const char *name() const override { return "BLE-MIDI"; }
  bool begin() override;
  void poll() override;
  bool connected() const override;
  bool send(const uint8_t *midi, size_t length) override;

  void onAdvertisement(const NimBLEAdvertisedDevice &device);
  void onDisconnected();
  void onMidiNotification(const uint8_t *data, size_t length);

private:
  void startScan();
  bool connectCandidate();

  NimBLEScan *scan_ = nullptr;
  NimBLEClient *client_ = nullptr;
  NimBLEClientCallbacks *clientCallbacks_ = nullptr;
  NimBLERemoteCharacteristic *midiCharacteristic_ = nullptr;
  NimBLEAdvertisedDevice *candidate_ = nullptr;
  bool initialized_ = false;
  bool connecting_ = false;
  bool connected_ = false;
  uint32_t candidateReadyAtMs_ = 0;
  uint32_t nextScanAtMs_ = 0;
  mutable portMUX_TYPE stateMux_ = portMUX_INITIALIZER_UNLOCKED;
};
