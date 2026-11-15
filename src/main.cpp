#include <Arduino.h>

#include "ble_midi_transport.h"
#include "board_pins.h"
#include "gp5_models.h"
#include "gp5_protocol.h"
#include "midi_transport.h"
#include "usb_midi_transport.h"
#include "web_ui.h"

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kDebounceMs = 25;
constexpr uint32_t kGp5CommandGapMs = 120;
constexpr uint8_t kMidiChannel = 1;

enum class TransportSelection { Ble, Usb };
constexpr TransportSelection kInitialTransport = TransportSelection::Usb;

enum class PedalSelection { PocketMaster, ValetonGp5 };
constexpr PedalSelection kPedal = PedalSelection::PocketMaster;

struct SwitchState {
  bool stableReleased = true;
  bool lastRawReleased = true;
  uint32_t rawChangedAtMs = 0;
};

enum class ActionType : uint8_t {
  BankDown,
  BankUp,
  ToggleDrive,
  ToggleDelay,
  ToggleReverb,
  PresetA,
  PresetB,
  PresetC,
  PresetD,
  ToggleTuner,
};

struct Action {
  ActionType type;
};

enum class Certainty : uint8_t { Unknown, Assumed, Confirmed };

constexpr size_t kActionQueueCapacity = 16;

SwitchState switchStates[BoardPins::kSwitchCount];
BleMidiTransport bleTransport;
UsbMidiTransport usbTransport;
MidiTransport *activeTransport = nullptr;
WebUi webUi;
Action actionQueue[kActionQueueCapacity];
size_t actionQueueRead = 0;
size_t actionQueueWrite = 0;
size_t actionQueueCount = 0;

uint8_t bank = 0;
uint8_t currentPreset = 0;
bool driveEnabled = false;
bool delayEnabled = false;
bool reverbEnabled = false;
bool tunerEnabled = false;
Certainty presetCertainty = Certainty::Unknown;
Certainty driveCertainty = Certainty::Unknown;
Certainty delayCertainty = Certainty::Unknown;
Certainty reverbCertainty = Certainty::Unknown;
Certainty tunerCertainty = Certainty::Unknown;
uint32_t stateRevision = 1;
bool transportWasConnected = false;
enum class Gp5Request : uint8_t {
  None,
  SyncStart,
  PatchNames,
  CurrentPreset,
  PatchState
};
Gp5Request gp5PendingRequest = Gp5Request::None;
uint32_t gp5RequestAtMs = 0;
uint32_t nextActionAtMs = 0;
bool gp5SyncInProgress = false;
bool gp5StateResponseInProgress = false;
uint32_t gp5StateResponseStartedMs = 0;
uint8_t gp5StateBuffer[512];
size_t gp5StateLength = 0;
uint8_t gp5NextFragment = 0;
char gp5PatchNames[100][11] = {};
uint32_t gp5PatchNameFragments = 0;
const char *driveModelName = "Drive";
const char *delayModelName = "Delay";
const char *reverbModelName = "Reverb";

void scheduleGp5Request(Gp5Request request, uint32_t delayMs = 100) {
  gp5PendingRequest = request;
  gp5RequestAtMs = millis() + delayMs;
}

void scheduleGp5StateRequest(uint32_t delayMs = 350) {
  if (gp5SyncInProgress) {
    return;
  }
  scheduleGp5Request(Gp5Request::PatchState, delayMs);
}

const char *certaintyName(Certainty certainty) {
  switch (certainty) {
  case Certainty::Assumed:
    return "assumed";
  case Certainty::Confirmed:
    return "confirmed";
  case Certainty::Unknown:
  default:
    return "unknown";
  }
}

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

bool enqueueAction(ActionType type) {
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
    ActionType type;
  };
  static constexpr NamedAction kActions[] = {
      {"bank_down", ActionType::BankDown},
      {"bank_up", ActionType::BankUp},
      {"toggle_drive", ActionType::ToggleDrive},
      {"toggle_delay", ActionType::ToggleDelay},
      {"toggle_reverb", ActionType::ToggleReverb},
      {"preset_a", ActionType::PresetA},
      {"preset_b", ActionType::PresetB},
      {"preset_c", ActionType::PresetC},
      {"preset_d", ActionType::PresetD},
      {"toggle_tuner", ActionType::ToggleTuner},
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
  char presetLabel[8];
  if (kPedal == PedalSelection::ValetonGp5 || currentPreset < 50) {
    snprintf(presetLabel, sizeof(presetLabel), "P%02u",
             static_cast<unsigned>(currentPreset + 1));
  } else {
    snprintf(presetLabel, sizeof(presetLabel), "F%02u",
             static_cast<unsigned>(currentPreset - 49));
  }
  char presetName[24];
  snprintf(presetName, sizeof(presetName), "%s %s",
           kPedal == PedalSelection::ValetonGp5 ? "GP-5" : "Pocket Master",
           presetLabel);
  const char *displayPresetName =
      kPedal == PedalSelection::ValetonGp5 &&
              gp5PatchNames[currentPreset][0] != 0
          ? gp5PatchNames[currentPreset]
          : presetName;

  String json;
  json.reserve(1024);
  json += F("{\"revision\":");
  json += stateRevision;
  json += F(",\"device\":\"");
  json += kPedal == PedalSelection::ValetonGp5 ? F("valeton_gp5")
                                               : F("pocket_master");
  json += F("\",\"connection\":\"");
  json += connected ? F("connected") : F("disconnected");
  json += F("\",\"bank\":");
  json += bank;
  json += F(",\"preset\":{\"absolute\":");
  json += currentPreset;
  json += F(",\"label\":");
  appendJsonString(json, presetLabel);
  json += F(",\"name\":");
  appendJsonString(json, displayPresetName);
  json += F(",\"certainty\":\"");
  json += certaintyName(presetCertainty);
  json += F("\"},\"bank_patches\":[");
  for (uint8_t slot = 0; slot < 4; ++slot) {
    const uint8_t patchIndex = static_cast<uint8_t>(bank * 4 + slot);
    char bankPatchLabel[8];
    snprintf(bankPatchLabel, sizeof(bankPatchLabel), "P%02u",
             static_cast<unsigned>(patchIndex + 1));
    char fallbackName[16];
    snprintf(fallbackName, sizeof(fallbackName), "Patch %c", 'A' + slot);
    if (slot != 0) {
      json += ',';
    }
    json += F("{\"absolute\":");
    json += static_cast<unsigned>(patchIndex);
    json += F(",\"label\":");
    appendJsonString(json, bankPatchLabel);
    json += F(",\"name\":");
    appendJsonString(json, gp5PatchNames[patchIndex][0] != 0
                               ? gp5PatchNames[patchIndex]
                               : fallbackName);
    json += '}';
  }
  json += F("],\"blocks\":[");

  struct BlockState {
    const char *id;
    const char *label;
    const char *effectName;
    bool enabled;
    Certainty certainty;
  };
  const BlockState blocks[] = {
      {"drive", "DRIVE", driveModelName, driveEnabled, driveCertainty},
      {"delay", "DELAY", delayModelName, delayEnabled, delayCertainty},
      {"reverb", "REVERB", reverbModelName, reverbEnabled, reverbCertainty},
      {"tuner", "TUNER", "Utility", tunerEnabled, tunerCertainty},
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
    json += certaintyName(blocks[i].certainty);
    json += F("\"}");
  }
  json += F("]}");
  return json;
}

void handleReceivedMidi(const uint8_t *midi, size_t length, void *) {
  uint8_t command = 0;
  uint8_t fragment = 0;
  uint8_t payload[128];
  size_t payloadLength = 0;
  if (!decodeGp5Fragment(midi, length, command, fragment, payload,
                         sizeof(payload), payloadLength)) {
    Serial.printf("Ignored GP-5 notification (%u MIDI bytes)\n",
                  static_cast<unsigned>(length));
    return;
  }

  Serial.printf("GP-5 response: command %u, fragment %u, payload %u bytes\n",
                static_cast<unsigned>(command),
                static_cast<unsigned>(fragment),
                static_cast<unsigned>(payloadLength));

  if (command == 0x15) {
    if (fragment < 20 && payloadLength == 100) {
      char names[5][11];
      if (decodeGp5PatchNames(fragment, payload, payloadLength, names)) {
        for (size_t i = 0; i < 5; ++i) {
          memcpy(gp5PatchNames[fragment * 5 + i], names[i], sizeof(names[i]));
        }
        gp5PatchNameFragments |= 1u << fragment;
        if (gp5PatchNameFragments == ((1u << 20) - 1)) {
          Serial.println("GP-5 patch names synchronized");
          ++stateRevision;
          scheduleGp5Request(Gp5Request::CurrentPreset, 50);
        }
      }
    } else if (fragment >= 20) {
      scheduleGp5Request(Gp5Request::CurrentPreset, 50);
    }
    return;
  }

  if (command == 1) {
    uint8_t confirmedPreset = 0;
    if (decodeGp5CurrentPreset(payload, payloadLength, confirmedPreset)) {
      currentPreset = confirmedPreset;
      bank = currentPreset / 4;
      presetCertainty = Certainty::Confirmed;
      ++stateRevision;
      Serial.printf("GP-5 current patch confirmed: P%02u %s\n",
                    static_cast<unsigned>(currentPreset + 1),
                    gp5PatchNames[currentPreset][0] != 0
                        ? gp5PatchNames[currentPreset]
                        : "(name unavailable)");
      gp5SyncInProgress = false;
      scheduleGp5StateRequest(150);
    }
    return;
  }

  if (command != 5) {
    return;
  }
  if (fragment == 0) {
    gp5StateLength = 0;
    gp5NextFragment = 0;
    gp5StateResponseInProgress = true;
    gp5StateResponseStartedMs = millis();
  }
  if (fragment != gp5NextFragment ||
      payloadLength > sizeof(gp5StateBuffer) - gp5StateLength) {
    gp5StateLength = 0;
    gp5NextFragment = 0;
    return;
  }
  memcpy(gp5StateBuffer + gp5StateLength, payload, payloadLength);
  gp5StateLength += payloadLength;
  ++gp5NextFragment;

  // Full fragments carry 100 decoded bytes; the shorter final fragment marks
  // completion without hard-coding a firmware-specific total dump size.
  if (payloadLength == 100) {
    return;
  }
  Gp5PatchDetails details;
  if (decodeGp5PatchDetails(gp5StateBuffer, gp5StateLength, details)) {
    driveEnabled = details.driveEnabled;
    delayEnabled = details.delayEnabled;
    reverbEnabled = details.reverbEnabled;
    if (const char *name = gp5EffectModelName(2, details.driveModelId)) {
      driveModelName = name;
    }
    if (const char *name = gp5EffectModelName(7, details.delayModelId)) {
      delayModelName = name;
    }
    if (const char *name = gp5EffectModelName(8, details.reverbModelId)) {
      reverbModelName = name;
    }
    driveCertainty = Certainty::Confirmed;
    delayCertainty = Certainty::Confirmed;
    reverbCertainty = Certainty::Confirmed;
    ++stateRevision;
    Serial.printf("GP-5 state: DRIVE %s (%s), DELAY %s (%s), REVERB %s (%s)\n",
                  driveEnabled ? "ON" : "OFF", driveModelName,
                  delayEnabled ? "ON" : "OFF", delayModelName,
                  reverbEnabled ? "ON" : "OFF", reverbModelName);
  }
  gp5StateLength = 0;
  gp5NextFragment = 0;
  gp5StateResponseInProgress = false;
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
  Serial.printf("Selected pedal profile: %s\n",
                kPedal == PedalSelection::ValetonGp5 ? "Valeton GP-5"
                                                     : "Pocket Master");
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

bool sendControlChange(uint8_t control, uint8_t value) {
  if (!activeTransport->connected()) {
    Serial.printf("  MIDI not sent: %s is not connected\n",
                  activeTransport->name());
    return false;
  }
  if (!activeTransport->sendControlChange(kMidiChannel, control, value)) {
    Serial.printf("  %s send failed\n", activeTransport->name());
    return false;
  }
  Serial.printf("  %s CC%u value %u sent\n", activeTransport->name(),
                static_cast<unsigned>(control), static_cast<unsigned>(value));
  return true;
}

void handlePressedSwitch(const char *name) {
  if (strcmp(name, "BANK-") == 0) {
    if (bank > 0) {
      --bank;
      ++stateRevision;
    }
    Serial.printf("  Bank %u (presets %u-%u)\n", static_cast<unsigned>(bank + 1),
                  static_cast<unsigned>(bank * 4 + 1),
                  static_cast<unsigned>(bank * 4 + 4));
    return;
  }
  if (strcmp(name, "BANK+") == 0) {
    if (bank < 24) {
      ++bank;
      ++stateRevision;
    }
    const unsigned first = bank * 4 + 1;
    Serial.printf("  Bank %u (presets %u-%u)\n", static_cast<unsigned>(bank + 1),
                  first, first + 3);
    return;
  }

  constexpr char kPresetSlots[] = "ABCD";
  const char *slot = strchr(kPresetSlots, name[0]);
  if (name[1] == '\0' && slot != nullptr) {
    const uint8_t presetIndex =
        bank * 4 + static_cast<uint8_t>(slot - kPresetSlots);
    if (kPedal == PedalSelection::ValetonGp5) {
      Serial.printf("  Select Valeton GP-5 preset P%02u\n",
                    static_cast<unsigned>(presetIndex + 1));
      uint8_t message[24];
      const size_t length =
          buildGp5PresetChange(presetIndex, message, sizeof(message));
      if (!activeTransport->connected()) {
        Serial.printf("  MIDI not sent: %s is not connected\n",
                      activeTransport->name());
      } else if (length == 0 || !activeTransport->send(message, length)) {
        Serial.printf("  %s GP-5 SysEx send failed\n",
                      activeTransport->name());
      } else {
        Serial.printf("  %s GP-5 preset SysEx sent\n",
                      activeTransport->name());
        currentPreset = presetIndex;
        presetCertainty = Certainty::Assumed;
        ++stateRevision;
        scheduleGp5StateRequest(350);
      }
    } else {
      Serial.printf("  Select Pocket Master preset %s%02u\n",
                    presetIndex < 50 ? "P" : "F",
                    static_cast<unsigned>(presetIndex < 50 ? presetIndex + 1
                                                           : presetIndex - 49));
      if (sendControlChange(1, presetIndex + 1)) {
        currentPreset = presetIndex;
        presetCertainty = Certainty::Assumed;
        ++stateRevision;
      }
    }
    return;
  }

  struct ToggleMapping {
    const char *name;
    uint8_t control;
    uint8_t gp5Block;
    bool *enabled;
    Certainty *certainty;
  };
  const bool gp5 = kPedal == PedalSelection::ValetonGp5;
  const ToggleMapping mappings[] = {
      {"DRIVE", static_cast<uint8_t>(gp5 ? 50 : 45), 2, &driveEnabled,
       &driveCertainty},
      {"DELAY", static_cast<uint8_t>(gp5 ? 56 : 50), 7, &delayEnabled,
       &delayCertainty},
      {"REVERB", static_cast<uint8_t>(gp5 ? 57 : 51), 8, &reverbEnabled,
       &reverbCertainty},
      {"UTILITY", 58, 0xff, &tunerEnabled, &tunerCertainty},
  };
  for (const ToggleMapping &mapping : mappings) {
    if (strcmp(name, mapping.name) != 0) {
      continue;
    }
    const bool target = !*mapping.enabled;
    Serial.printf("  %s %s\n", mapping.name,
                  target ? "ON (locally assumed)" : "OFF (locally assumed)");
    bool sent = false;
    if (gp5 && mapping.gp5Block != 0xff) {
      uint8_t message[30];
      const size_t length = buildGp5BlockToggle(
          mapping.gp5Block, target, message, sizeof(message));
      if (!activeTransport->connected()) {
        Serial.printf("  MIDI not sent: %s is not connected\n",
                      activeTransport->name());
      } else if (length == 0 || !activeTransport->send(message, length)) {
        Serial.printf("  %s GP-5 block SysEx send failed\n",
                      activeTransport->name());
      } else {
        Serial.printf("  %s GP-5 block SysEx sent\n",
                      activeTransport->name());
        sent = true;
        scheduleGp5StateRequest();
      }
    } else {
      sent = sendControlChange(mapping.control, target ? 127 : 0);
    }
    if (sent) {
      *mapping.enabled = target;
      *mapping.certainty = Certainty::Assumed;
      ++stateRevision;
    }
    return;
  }
}

bool enqueueSwitchAction(const char *name) {
  struct SwitchAction {
    const char *name;
    ActionType type;
  };
  static constexpr SwitchAction kSwitchActions[] = {
      {"BANK-", ActionType::BankDown},
      {"BANK+", ActionType::BankUp},
      {"DRIVE", ActionType::ToggleDrive},
      {"DELAY", ActionType::ToggleDelay},
      {"REVERB", ActionType::ToggleReverb},
      {"A", ActionType::PresetA},
      {"B", ActionType::PresetB},
      {"C", ActionType::PresetC},
      {"D", ActionType::PresetD},
      {"UTILITY", ActionType::ToggleTuner},
  };
  for (const SwitchAction &action : kSwitchActions) {
    if (strcmp(name, action.name) == 0) {
      return enqueueAction(action.type);
    }
  }
  return false;
}

void processQueuedActions() {
  if (actionQueueCount == 0 || gp5SyncInProgress) {
    return;
  }
  if (gp5StateResponseInProgress) {
    if (millis() - gp5StateResponseStartedMs < 1000) {
      return;
    }
    gp5StateResponseInProgress = false;
  }
  if (static_cast<int32_t>(millis() - nextActionAtMs) < 0) {
    return;
  }

  // The GP-5 becomes unreliable when several SysEx writes and a state-read
  // request arrive back-to-back. Serialize actions with a small quiet gap;
  // each action reschedules readback until after the burst has drained.
  const Action action = actionQueue[actionQueueRead];
  actionQueueRead = (actionQueueRead + 1) % kActionQueueCapacity;
  --actionQueueCount;
  switch (action.type) {
    case ActionType::BankDown:
      handlePressedSwitch("BANK-");
      break;
    case ActionType::BankUp:
      handlePressedSwitch("BANK+");
      break;
    case ActionType::ToggleDrive:
      handlePressedSwitch("DRIVE");
      break;
    case ActionType::ToggleDelay:
      handlePressedSwitch("DELAY");
      break;
    case ActionType::ToggleReverb:
      handlePressedSwitch("REVERB");
      break;
    case ActionType::PresetA:
      handlePressedSwitch("A");
      break;
    case ActionType::PresetB:
      handlePressedSwitch("B");
      break;
    case ActionType::PresetC:
      handlePressedSwitch("C");
      break;
    case ActionType::PresetD:
      handlePressedSwitch("D");
      break;
    case ActionType::ToggleTuner:
      handlePressedSwitch("UTILITY");
      break;
  }
  nextActionAtMs = millis() + kGp5CommandGapMs;
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
  if (transportConnected != transportWasConnected) {
    ++stateRevision;
  }
  if (transportConnected && !transportWasConnected &&
      kPedal == PedalSelection::ValetonGp5) {
    gp5PatchNameFragments = 0;
    gp5SyncInProgress = true;
    gp5StateResponseInProgress = false;
    scheduleGp5Request(Gp5Request::SyncStart);
  }
  if (!transportConnected && transportWasConnected) {
    gp5PendingRequest = Gp5Request::None;
    gp5SyncInProgress = false;
    gp5StateResponseInProgress = false;
  }
  transportWasConnected = transportConnected;
  if (transportConnected && actionQueueCount == 0 &&
      gp5PendingRequest != Gp5Request::None &&
      static_cast<int32_t>(millis() - gp5RequestAtMs) >= 0) {
    uint8_t request[14];
    const Gp5Request requestType = gp5PendingRequest;
    gp5PendingRequest = Gp5Request::None;
    size_t length = 0;
    const char *description = nullptr;
    switch (requestType) {
    case Gp5Request::SyncStart:
      length = buildGp5SyncStartRequest(request, sizeof(request));
      description = "synchronization start";
      break;
    case Gp5Request::PatchNames:
      length = buildGp5PatchListRequest(request, sizeof(request));
      description = "patch names";
      break;
    case Gp5Request::CurrentPreset:
      length = buildGp5CurrentPresetRequest(request, sizeof(request));
      description = "current patch number";
      break;
    case Gp5Request::PatchState:
      length = buildGp5PatchStateRequest(request, sizeof(request));
      description = "current patch state";
      break;
    case Gp5Request::None:
      break;
    }
    if (length != 0 && activeTransport->send(request, length)) {
      Serial.printf("Requested GP-5 %s\n", description);
      if (requestType == Gp5Request::SyncStart) {
        scheduleGp5Request(Gp5Request::PatchNames, 100);
      }
    } else if (requestType != Gp5Request::None) {
      scheduleGp5Request(requestType, 500);
    }
  }
  scanSwitches();
  processQueuedActions();
  delay(1);
}
