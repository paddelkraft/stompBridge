#include "gp5_protocol.h"

#include <cstring>

namespace {

uint8_t crc8(const uint8_t *nibbles, size_t length) {
  uint8_t crc = 0;
  for (size_t i = 0; i + 1 < length; i += 2) {
    const uint8_t value = static_cast<uint8_t>(((nibbles[i] & 0x0f) << 4) |
                                                (nibbles[i + 1] & 0x0f));
    crc ^= value;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) != 0 ? static_cast<uint8_t>((crc << 1) ^ 0x07)
                               : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

} // namespace

size_t buildGp5PresetChange(uint8_t presetIndex, uint8_t *output,
                            size_t capacity) {
  if (output == nullptr || capacity < 22 || presetIndex > 99) {
    return 0;
  }

  const uint8_t payload[] = {
      0x04, 0x03, static_cast<uint8_t>((presetIndex >> 4) & 0x0f),
      static_cast<uint8_t>(presetIndex & 0x0f), 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00,
  };

  size_t index = 0;
  output[index++] = 0xf0;
  const size_t crcIndex = index;
  output[index++] = 0x00;
  output[index++] = 0x00;
  output[index++] = 0x00;
  output[index++] = 0x01;
  output[index++] = 0x00;
  output[index++] = 0x00;
  output[index++] = 0x00;
  output[index++] = 0x06;
  output[index++] = 0x01;
  output[index++] = 0x01;
  memcpy(output + index, payload, sizeof(payload));
  index += sizeof(payload);
  output[index++] = 0xf7;

  const uint8_t crc = crc8(output + 3, index - 4);
  output[crcIndex] = static_cast<uint8_t>(crc >> 4);
  output[crcIndex + 1] = static_cast<uint8_t>(crc & 0x0f);
  return index;
}

size_t buildGp5BlockToggle(uint8_t blockIndex, bool enabled, uint8_t *output,
                           size_t capacity) {
  if (output == nullptr || capacity < 30 || blockIndex > 9) {
    return 0;
  }

  // Decodes to command 0x11 0x49 followed by a little-endian block index,
  // enabled state, and the reserved bytes used by the GP-5 editor protocol.
  const uint8_t payload[] = {
      0x04, 0x09, 0x00, blockIndex, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, static_cast<uint8_t>(enabled ? 1 : 0), 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
  };

  size_t index = 0;
  output[index++] = 0xf0;
  const size_t crcIndex = index;
  output[index++] = 0x00;
  output[index++] = 0x00;
  output[index++] = 0x00;
  output[index++] = 0x01;
  output[index++] = 0x00;
  output[index++] = 0x00;
  output[index++] = 0x00;
  output[index++] = 0x0a;
  output[index++] = 0x01;
  output[index++] = 0x01;
  memcpy(output + index, payload, sizeof(payload));
  index += sizeof(payload);
  output[index++] = 0xf7;

  const uint8_t crc = crc8(output + 3, index - 4);
  output[crcIndex] = static_cast<uint8_t>(crc >> 4);
  output[crcIndex + 1] = static_cast<uint8_t>(crc & 0x0f);
  return index;
}

size_t buildGp5PatchStateRequest(uint8_t *output, size_t capacity) {
  if (output == nullptr || capacity < 14) {
    return 0;
  }
  const uint8_t request[] = {0xf0, 0x00, 0x09, 0x00, 0x01, 0x00, 0x00,
                             0x00, 0x02, 0x01, 0x02, 0x04, 0x01, 0xf7};
  memcpy(output, request, sizeof(request));
  return sizeof(request);
}

size_t buildGp5SyncStartRequest(uint8_t *output, size_t capacity) {
  if (output == nullptr || capacity < 14) {
    return 0;
  }
  const uint8_t request[] = {0xf0, 0x00, 0x0e, 0x00, 0x01, 0x00, 0x00,
                             0x00, 0x02, 0x01, 0x02, 0x04, 0x00, 0xf7};
  memcpy(output, request, sizeof(request));
  return sizeof(request);
}

size_t buildGp5PatchListRequest(uint8_t *output, size_t capacity) {
  if (output == nullptr || capacity < 14) {
    return 0;
  }
  const uint8_t request[] = {0xf0, 0x03, 0x05, 0x00, 0x01, 0x00, 0x00,
                             0x00, 0x02, 0x01, 0x02, 0x02, 0x04, 0xf7};
  memcpy(output, request, sizeof(request));
  return sizeof(request);
}

size_t buildGp5CurrentPresetRequest(uint8_t *output, size_t capacity) {
  if (output == nullptr || capacity < 14) {
    return 0;
  }
  const uint8_t request[] = {0xf0, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00,
                             0x00, 0x02, 0x01, 0x02, 0x04, 0x03, 0xf7};
  memcpy(output, request, sizeof(request));
  return sizeof(request);
}

bool decodeGp5Fragment(const uint8_t *midi, size_t length, uint8_t &command,
                       uint8_t &fragmentIndex, uint8_t *payload,
                       size_t payloadCapacity, size_t &payloadLength) {
  payloadLength = 0;
  if (midi == nullptr || payload == nullptr || length < 10 || midi[0] != 0xf0 ||
      midi[length - 1] != 0xf7) {
    return false;
  }
  for (size_t index = 1; index + 1 < length; ++index) {
    if (midi[index] > 0x0f) {
      return false;
    }
  }
  const uint8_t expectedCrc =
      static_cast<uint8_t>((midi[1] << 4) | midi[2]);
  if (crc8(midi + 3, length - 4) != expectedCrc) {
    return false;
  }

  command = static_cast<uint8_t>((midi[3] << 4) | midi[4]);
  fragmentIndex = static_cast<uint8_t>((midi[5] << 4) | midi[6]);
  payloadLength = static_cast<size_t>((midi[7] << 4) | midi[8]);
  if (payloadLength > payloadCapacity || length != 10 + payloadLength * 2) {
    payloadLength = 0;
    return false;
  }
  for (size_t index = 0; index < payloadLength; ++index) {
    payload[index] = static_cast<uint8_t>((midi[9 + index * 2] << 4) |
                                          midi[10 + index * 2]);
  }
  return true;
}

namespace {

uint16_t readLittleEndian16(const uint8_t *value) {
  return static_cast<uint16_t>(value[0]) |
         static_cast<uint16_t>(value[1]) << 8;
}

uint32_t readBigEndian32(const uint8_t *value) {
  return static_cast<uint32_t>(value[0]) << 24 |
         static_cast<uint32_t>(value[1]) << 16 |
         static_cast<uint32_t>(value[2]) << 8 |
         static_cast<uint32_t>(value[3]);
}

bool findTlv(const uint8_t *data, size_t length, uint16_t wanted,
             const uint8_t *&value, size_t &valueLength) {
  size_t offset = 0;
  while (offset + 4 <= length) {
    const uint16_t identifier = readLittleEndian16(data + offset);
    const size_t fieldLength = readLittleEndian16(data + offset + 2);
    const size_t start = offset + 4;
    if (fieldLength > length - start) {
      return false;
    }
    if (identifier == wanted) {
      value = data + start;
      valueLength = fieldLength;
      return true;
    }
    offset = start + fieldLength;
  }
  return false;
}

} // namespace

bool decodeGp5BlockStates(const uint8_t *logical, size_t length,
                          bool &driveEnabled, bool &delayEnabled,
                          bool &reverbEnabled) {
  Gp5PatchDetails details;
  if (!decodeGp5PatchDetails(logical, length, details)) {
    return false;
  }
  driveEnabled = details.driveEnabled;
  delayEnabled = details.delayEnabled;
  reverbEnabled = details.reverbEnabled;
  return true;
}

bool decodeGp5PatchDetails(const uint8_t *logical, size_t length,
                           Gp5PatchDetails &details) {
  if (logical == nullptr) {
    return false;
  }

  // Command-5 patch data is returned in four 100-byte fragments plus a
  // shorter final fragment. The state bitmap is in fragment 0 and the model
  // identifiers start fragment 1, after decoding each nibble pair.
  if (length >= 128) {
    details.driveEnabled = (logical[70] & (1u << 2)) != 0;
    details.delayEnabled = (logical[70] & (1u << 7)) != 0;
    details.reverbEnabled = (logical[71] & (1u << 0)) != 0;
    details.driveModelId = readBigEndian32(logical + 100);
    details.delayModelId = readBigEndian32(logical + 120);
    details.reverbModelId = readBigEndian32(logical + 124);
    return true;
  }

  // Also accept the decoded TLV representation used by earlier firmware.
  if (length < 2 || logical[0] != 0x12 || logical[1] != 0x41) {
    return false;
  }
  const uint8_t *moduleSection = nullptr;
  size_t moduleSectionLength = 0;
  if (!findTlv(logical + 2, length - 2, 0x0002, moduleSection,
               moduleSectionLength)) {
    return false;
  }
  const uint8_t *maskBytes = nullptr;
  size_t maskLength = 0;
  if (!findTlv(moduleSection, moduleSectionLength, 0x3001, maskBytes,
               maskLength) ||
      maskLength != 4) {
    return false;
  }
  const uint32_t mask = static_cast<uint32_t>(maskBytes[0]) |
                        static_cast<uint32_t>(maskBytes[1]) << 8 |
                        static_cast<uint32_t>(maskBytes[2]) << 16 |
                        static_cast<uint32_t>(maskBytes[3]) << 24;
  const uint8_t *modelBytes = nullptr;
  size_t modelLength = 0;
  details.driveEnabled = (mask & (1u << 2)) != 0;
  details.delayEnabled = (mask & (1u << 7)) != 0;
  details.reverbEnabled = (mask & (1u << 8)) != 0;
  if (findTlv(moduleSection, moduleSectionLength, 0x3003, modelBytes,
              modelLength) &&
      modelLength >= 10 * sizeof(uint32_t)) {
    details.driveModelId = readBigEndian32(modelBytes + 2 * sizeof(uint32_t));
    details.delayModelId = readBigEndian32(modelBytes + 7 * sizeof(uint32_t));
    details.reverbModelId = readBigEndian32(modelBytes + 8 * sizeof(uint32_t));
  }
  return true;
}

bool decodeGp5CurrentPreset(const uint8_t *payload, size_t length,
                            uint8_t &presetIndex) {
  if (payload == nullptr || length < 4 || payload[0] != 0x12 ||
      payload[1] != 0x43 || payload[2] > 99) {
    return false;
  }
  presetIndex = payload[2];
  return true;
}

bool decodeGp5PatchNames(uint8_t fragmentIndex, const uint8_t *payload,
                         size_t length, char names[5][11]) {
  if (payload == nullptr || names == nullptr || fragmentIndex >= 20 ||
      length != 100) {
    return false;
  }
  for (size_t nameIndex = 0; nameIndex < 5; ++nameIndex) {
    const size_t start = 6 + nameIndex * 20;
    for (size_t character = 0; character < 10; ++character) {
      const uint8_t value = payload[start + character];
      names[nameIndex][character] =
          value >= 0x20 && value <= 0x7e ? static_cast<char>(value) : ' ';
    }
    names[nameIndex][10] = '\0';
    for (int character = 9;
         character >= 0 && names[nameIndex][character] == ' '; --character) {
      names[nameIndex][character] = '\0';
    }
  }
  return true;
}
