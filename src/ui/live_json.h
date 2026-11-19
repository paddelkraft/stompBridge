#pragma once

#include "core/read_model.h"
#include "live_ingress.h"

namespace stomp::ui {

enum class JsonStatus : uint8_t { Ok, Malformed, VersionMismatch, Unsupported, TooLarge };
constexpr size_t kMaxJsonCommandBytes = 256;
constexpr size_t kMaxJsonSnapshotBytes = 8192;

struct HelloRequest {
  uint8_t minimumVersion = 0;
  uint8_t maximumVersion = 0;
  uint32_t epoch = 0;
};

struct UiRequest {
  uint32_t requestId = 0;
  uint32_t epoch = 0;
  SessionGeneration generation = 0;
  CommandKind command = CommandKind::SelectPreset;
  uint16_t target = 0;
  bool enabled = false;
  bool snapshot = false;
};

JsonStatus decodeHello(const uint8_t *bytes, size_t length, HelloRequest &out);
JsonStatus decodeRequest(const uint8_t *bytes, size_t length, UiRequest &out);
size_t encodeSnapshot(const ReadSnapshot &snapshot, uint32_t epoch,
                      bool bridgeConnected, char *out, size_t capacity,
                      const char *deviceId = "unknown");
size_t encodeResult(const RequestResult &result, char *out, size_t capacity);

} // namespace stomp::ui
