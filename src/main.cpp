#include <Arduino.h>

#include "ble_midi_transport.h"
#include "board_pins.h"
#include "midi_transport.h"
#include "pedal_plugin.h"
#include "usb_midi_transport.h"
#include "web_ui.h"

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kDebounceMs = 25;
constexpr uint32_t kPedalCommandGapMs = 120;

enum class TransportSelection { Ble, Usb };
constexpr TransportSelection kInitialTransport = TransportSelection::Usb;

struct SwitchState {
  bool stableReleased = true;
  bool lastRawReleased = true;
  uint32_t rawChangedAtMs = 0;
};

struct Action {
  PedalAction type;
};

constexpr size_t kActionQueueCapacity = 16;

SwitchState switchStates[BoardPins::kSwitchCount];
BleMidiTransport bleTransport;
UsbMidiTransport usbTransport;
MidiTransport *activeTransport = nullptr;
PedalPlugin *activePlugin = nullptr;
WebUi webUi;
Action actionQueue[kActionQueueCapacity];
size_t actionQueueRead = 0;
size_t actionQueueWrite = 0;
size_t actionQueueCount = 0;

uint32_t stateRevision = 1;
bool transportWasConnected = false;
uint32_t nextActionAtMs = 0;

void appendJsonString(String &json, const char *value) {
  json += '"';
  for (const char *character = value; character != nullptr && *character != 0;
       ++character) {
    if (*character == '"' || *character == '\\') {
      json += '\\';
    }
    if (static_cast<uint8_t>(*character) >= 0x20) {
      json += *character;
    }
  }
  json += '"';
}

bool enqueueAction(PedalAction type) {
  if (actionQueueCount == kActionQueueCapacity) {
    return false;
  }
  actionQueue[actionQueueWrite] = {.type = type};
  actionQueueWrite = (actionQueueWrite + 1) % kActionQueueCapacity;
  ++actionQueueCount;
  return true;
}

bool enqueueNamedAction(const char *name) {
  struct NamedAction {
    const char *name;
    PedalAction type;
  };
  static constexpr NamedAction kActions[] = {
      {"bank_down", PedalAction::BankDown},
      {"bank_up", PedalAction::BankUp},
      {"toggle_drive", PedalAction::ToggleDrive},
      {"toggle_delay", PedalAction::ToggleDelay},
      {"toggle_reverb", PedalAction::ToggleReverb},
      {"preset_a", PedalAction::PresetA},
      {"preset_b", PedalAction::PresetB},
      {"preset_c", PedalAction::PresetC},
      {"preset_d", PedalAction::PresetD},
      {"toggle_tuner", PedalAction::ToggleTuner},
  };
  for (const NamedAction &action : kActions) {
    if (strcmp(name, action.name) == 0) {
      return enqueueAction(action.type);
    }
  }
  return false;
}

bool enqueueBrowserAction(const char *name, void *) {
  return enqueueNamedAction(name);
}

String buildWebState(void *) {
  const bool connected = activeTransport != nullptr && activeTransport->connected();
  static const PedalState emptyState;
  const PedalState &state = activePlugin != nullptr ? activePlugin->state()
                                                     : emptyState;
  char presetLabel[8] = "--";
  if (activePlugin != nullptr) {
    activePlugin->formatPreset(state.currentPreset, presetLabel,
                               sizeof(presetLabel));
  }
  char presetFallback[40];
  snprintf(presetFallback, sizeof(presetFallback), "%s %s",
           activePlugin != nullptr ? activePlugin->name() : "No pedal",
           presetLabel);
  const char *patchName = activePlugin != nullptr
                              ? activePlugin->presetName(state.currentPreset)
                              : "";
  const char *displayPresetName = patchName[0] != '\0' ? patchName : presetFallback;

  String json;
  json.reserve(1024);
  json += F("{\"revision\":");
  json += stateRevision;
  json += F(",\"device\":");
  appendJsonString(json, activePlugin != nullptr ? activePlugin->id() : "unknown");
  json += F(",\"connection\":\"");
  json += connected ? F("connected") : F("disconnected");
  json += F("\",\"bank\":");
  json += state.bank;
  json += F(",\"preset\":{\"absolute\":");
  json += state.currentPreset;
  json += F(",\"label\":");
  appendJsonString(json, presetLabel);
  json += F(",\"name\":");
  appendJsonString(json, displayPresetName);
  json += F(",\"certainty\":\"");
  json += pedalCertaintyName(state.presetCertainty);
  json += F("\"},\"bank_patches\":[");
  for (uint8_t slot = 0; slot < 4; ++slot) {
    const uint8_t patchIndex = static_cast<uint8_t>(state.bank * 4 + slot);
    char bankPatchLabel[8] = "--";
    if (activePlugin != nullptr) {
      activePlugin->formatPreset(patchIndex, bankPatchLabel,
                                 sizeof(bankPatchLabel));
    }
    char fallbackName[16];
    snprintf(fallbackName, sizeof(fallbackName), "Patch %c", 'A' + slot);
    const char *bankPatchName = activePlugin != nullptr
                                    ? activePlugin->presetName(patchIndex)
                                    : "";
    if (slot != 0) {
      json += ',';
    }
    json += F("{\"absolute\":");
    json += static_cast<unsigned>(patchIndex);
    json += F(",\"label\":");
    appendJsonString(json, bankPatchLabel);
    json += F(",\"name\":");
    appendJsonString(json, bankPatchName[0] != '\0' ? bankPatchName
                                                      : fallbackName);
    json += '}';
  }
  json += F("],\"blocks\":[");

  struct BlockState {
    const char *id;
    const char *label;
    const char *effectName;
    bool enabled;
    PedalCertainty certainty;
  };
  const BlockState blocks[] = {
      {"drive", "DRIVE", state.driveModelName, state.driveEnabled,
       state.driveCertainty},
      {"delay", "DELAY", state.delayModelName, state.delayEnabled,
       state.delayCertainty},
      {"reverb", "REVERB", state.reverbModelName, state.reverbEnabled,
       state.reverbCertainty},
      {"tuner", "TUNER", "Utility", state.tunerEnabled,
       state.tunerCertainty},
  };
  for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i) {
    if (i != 0) {
      json += ',';
    }
    json += F("{\"id\":\"");
    json += blocks[i].id;
    json += F("\",\"label\":\"");
    json += blocks[i].label;
    json += F("\",\"effect_name\":");
    appendJsonString(json, blocks[i].effectName);
    json += F(",\"enabled\":");
    json += blocks[i].enabled ? F("true") : F("false");
    json += F(",\"certainty\":\"");
    json += pedalCertaintyName(blocks[i].certainty);
    json += F("\"}");
  }
  json += F("]}");
  return json;
}

void handleReceivedMidi(const uint8_t *midi, size_t length, void *) {
  if (activePlugin == nullptr) {
    Serial.printf("Ignoring MIDI data without a selected pedal plugin (%u bytes)\n",
                  static_cast<unsigned>(length));
    return;
  }
  activePlugin->handleMidi(midi, length, millis());
  ++stateRevision;
}

MidiTransport &selectTransport(TransportSelection selection) {
  return selection == TransportSelection::Ble
             ? static_cast<MidiTransport &>(bleTransport)
             : static_cast<MidiTransport &>(usbTransport);
}

void printStartupBanner() {
  Serial.println();
  Serial.println("StompBridge transport-independent test");
  Serial.printf("Flash: %u MB; PSRAM: %u MB.\n",
                static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)),
                static_cast<unsigned>(ESP.getPsramSize() / (1024 * 1024)));
  Serial.printf("Selected transport: %s\n", activeTransport->name());
  Serial.println("Pedal plugin: auto-detected from connected USB VID/PID");
  Serial.printf("Monitoring %u switches with %u ms debounce.\n",
                static_cast<unsigned>(BoardPins::kSwitchCount),
                static_cast<unsigned>(kDebounceMs));
  Serial.println("Initial switch state:");
  for (size_t i = 0; i < BoardPins::kSwitchCount; ++i) {
    Serial.printf("  %-7s IO%u %s\n", BoardPins::kSwitchNames[i],
                  static_cast<unsigned>(BoardPins::kSwitches[i]),
                  switchStates[i].stableReleased ? "RELEASED"
                                                  : "PRESSED/CHECK WIRING");
  }
}

void initializeSwitches() {
  for (size_t i = 0; i < BoardPins::kSwitchCount; ++i) {
    const uint8_t pin = BoardPins::kSwitches[i];
    pinMode(pin, INPUT_PULLUP);
    const bool released = digitalRead(pin) == HIGH;
    switchStates[i] = {
        .stableReleased = released,
        .lastRawReleased = released,
        .rawChangedAtMs = millis(),
    };
  }
}

bool enqueueSwitchAction(const char *name) {
  struct SwitchAction {
    const char *name;
    PedalAction type;
  };
  static constexpr SwitchAction kSwitchActions[] = {
      {"BANK-", PedalAction::BankDown},
      {"BANK+", PedalAction::BankUp},
      {"DRIVE", PedalAction::ToggleDrive},
      {"DELAY", PedalAction::ToggleDelay},
      {"REVERB", PedalAction::ToggleReverb},
      {"A", PedalAction::PresetA},
      {"B", PedalAction::PresetB},
      {"C", PedalAction::PresetC},
      {"D", PedalAction::PresetD},
      {"UTILITY", PedalAction::ToggleTuner},
  };
  for (const SwitchAction &action : kSwitchActions) {
    if (strcmp(name, action.name) == 0) {
      return enqueueAction(action.type);
    }
  }
  return false;
}

void processQueuedActions() {
  if (actionQueueCount == 0 || activePlugin == nullptr ||
      !activeTransport->connected()) {
    return;
  }
  const uint32_t now = millis();
  if (!activePlugin->readyForAction(now) ||
      static_cast<int32_t>(now - nextActionAtMs) < 0) {
    return;
  }

  const Action action = actionQueue[actionQueueRead];
  actionQueueRead = (actionQueueRead + 1) % kActionQueueCapacity;
  --actionQueueCount;
  if (activePlugin->handleAction(action.type, *activeTransport, now)) {
    ++stateRevision;
  }
  nextActionAtMs = now + kPedalCommandGapMs;
}

void scanSwitches() {
  const uint32_t now = millis();
  for (size_t i = 0; i < BoardPins::kSwitchCount; ++i) {
    SwitchState &state = switchStates[i];
    const uint8_t pin = BoardPins::kSwitches[i];
    const bool rawReleased = digitalRead(pin) == HIGH;
    if (rawReleased != state.lastRawReleased) {
      state.lastRawReleased = rawReleased;
      state.rawChangedAtMs = now;
    }
    if (rawReleased != state.stableReleased &&
        now - state.rawChangedAtMs >= kDebounceMs) {
      state.stableReleased = rawReleased;
      Serial.printf("SW%u %-7s IO%u %s\n", static_cast<unsigned>(i + 1),
                    BoardPins::kSwitchNames[i], static_cast<unsigned>(pin),
                    rawReleased ? "RELEASED" : "PRESSED");
      if (!rawReleased) {
        if (!enqueueSwitchAction(BoardPins::kSwitchNames[i])) {
          Serial.println("  Action queue full; switch press dropped");
        }
      }
    }
  }
}

} // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(500);
  initializeSwitches();
  activeTransport = &selectTransport(kInitialTransport);
  activeTransport->setReceiveHandler(handleReceivedMidi);
  printStartupBanner();
  if (!activeTransport->begin()) {
    Serial.printf("%s failed to initialize\n", activeTransport->name());
  }
  if (webUi.begin(buildWebState, enqueueBrowserAction)) {
    Serial.println("Web UI ready:");
    Serial.printf("  Wi-Fi: %s\n", webUi.ssid());
    Serial.printf("  Password: %s\n", webUi.password());
    Serial.println("  Open: http://192.168.4.1/");
  } else {
    Serial.println("Web UI Wi-Fi failed to initialize; MIDI remains available");
  }
}

void loop() {
  activeTransport->poll();
  webUi.poll();
  const bool transportConnected = activeTransport->connected();
  MidiDeviceIdentity identity;
  PedalPlugin *detectedPlugin =
      transportConnected && activeTransport->connectedDevice(identity)
          ? findPedalPlugin(identity)
          : nullptr;
  if (transportConnected != transportWasConnected) {
    ++stateRevision;
  }
  if (transportConnected &&
      (!transportWasConnected || detectedPlugin != activePlugin)) {
    if (activePlugin != nullptr && detectedPlugin != activePlugin) {
      activePlugin->onDisconnected();
    }
    activePlugin = detectedPlugin;
    actionQueueCount = 0;
    actionQueueRead = actionQueueWrite;
    if (activePlugin != nullptr) {
      activePlugin->onConnected(*activeTransport);
      Serial.printf("Selected pedal plugin: %s (%04X:%04X)\n",
                    activePlugin->name(), identity.vendorId,
                    identity.productId);
    } else {
      Serial.printf("No pedal plugin for USB device %04X:%04X\n",
                    identity.vendorId, identity.productId);
    }
    ++stateRevision;
  } else if (!transportConnected && transportWasConnected) {
    if (activePlugin != nullptr) {
      activePlugin->onDisconnected();
    }
    actionQueueCount = 0;
    actionQueueRead = actionQueueWrite;
  }
  transportWasConnected = transportConnected;
  if (transportConnected && activePlugin != nullptr) {
    activePlugin->poll(*activeTransport, millis());
  }
  scanSwitches();
  processQueuedActions();
  delay(1);
}
