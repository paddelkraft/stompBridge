#pragma once

#include <stddef.h>
#include <stdint.h>

// Builds the MIDI SysEx message used by the Valeton GP-5 to select an
// absolute preset. The transport is responsible for BLE-MIDI framing.
size_t buildGp5PresetChange(uint8_t presetIndex, uint8_t *output,
                            size_t capacity);

// Builds the GP-5 proprietary SysEx message for enabling or bypassing one of
// its effect blocks. Block indices are 0..9 (NR, PRE, DST, AMP, CAB, EQ, MOD,
// DLY, RVB, SnapTone).
size_t buildGp5BlockToggle(uint8_t blockIndex, bool enabled, uint8_t *output,
                           size_t capacity);

// Builds the read-only request for the current patch data. The response
// contains the pedal's actual effect-block states.
size_t buildGp5PatchStateRequest(uint8_t *output, size_t capacity);

// Builds read-only requests used to synchronize the patch-name table and the
// currently selected preset.
size_t buildGp5SyncStartRequest(uint8_t *output, size_t capacity);
size_t buildGp5PatchListRequest(uint8_t *output, size_t capacity);
size_t buildGp5CurrentPresetRequest(uint8_t *output, size_t capacity);

// Decodes one GP-5 nibble-expanded SysEx response fragment. Returns only the
// decoded fragment payload; BLE-MIDI framing must already be removed.
bool decodeGp5Fragment(const uint8_t *midi, size_t length, uint8_t &command,
                       uint8_t &fragmentIndex, uint8_t *payload,
                       size_t payloadCapacity, size_t &payloadLength);

// Extracts the confirmed DST, DLY, and RVB enable flags from a complete
// current-patch response assembled from command-5 fragments.
bool decodeGp5BlockStates(const uint8_t *logical, size_t length,
                          bool &driveEnabled, bool &delayEnabled,
                          bool &reverbEnabled);

struct Gp5PatchDetails {
  bool driveEnabled = false;
  bool delayEnabled = false;
  bool reverbEnabled = false;
  uint32_t driveModelId = 0;
  uint32_t delayModelId = 0;
  uint32_t reverbModelId = 0;
};

// Extracts block states and model identifiers from a complete current-patch
// response. Model identifiers are the four-byte values used by the GP-5.
bool decodeGp5PatchDetails(const uint8_t *logical, size_t length,
                           Gp5PatchDetails &details);

// Decodes the payload of the GP-5 current-patch-number response.
bool decodeGp5CurrentPreset(const uint8_t *payload, size_t length,
                            uint8_t &presetIndex);

// Decodes five ten-character names from one patch-list fragment (0..19).
bool decodeGp5PatchNames(uint8_t fragmentIndex, const uint8_t *payload,
                         size_t length, char names[5][11]);
