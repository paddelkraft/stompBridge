#pragma once

#include <cstddef>
#include <cstdint>

namespace stomp::ble_ui {

// ATT attribute payload, including this 12-byte transport header. ATT MTU 23
// therefore carries 8 bytes of frame data in each 20-byte write/notify.
constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kHeaderSize = 12;
constexpr size_t kMaxAttPayload = 514; // ATT MTU 517 minus the 3-byte ATT header.
constexpr size_t kMaxMessage = 8192;
constexpr size_t kMaxFragments = 1024;
constexpr uint32_t kFragmentTimeoutMs = 3000;

enum class MessageType : uint8_t { Hello = 1, Command = 2, Snapshot = 3, Result = 4, Resync = 5 };
enum class FrameError : uint8_t { None, Incomplete, Complete, Malformed, VersionMismatch, TooLarge, OutOfOrder, Duplicate, Timeout };

struct Frame {
  uint16_t length = 0;
  uint8_t bytes[kMaxAttPayload] = {};
};

struct Message {
  MessageType type = MessageType::Hello;
  uint16_t transfer = 0;
  uint16_t length = 0;
  uint8_t bytes[kMaxMessage] = {};
};

size_t fragmentCount(size_t messageLength, size_t attPayload);
FrameError encodeFragment(MessageType type, uint16_t transfer,
                          const uint8_t *message, size_t messageLength,
                          size_t index, size_t attPayload, Frame &out);

class Reassembler {
public:
  FrameError accept(const uint8_t *frame, size_t length, size_t attPayload,
                    uint32_t nowMs, Message &completed);
  void reset();
  bool active() const { return active_; }

private:
  Message pending_;
  uint16_t nextIndex_ = 0;
  uint16_t fragmentTotal_ = 0;
  uint16_t expectedLength_ = 0;
  uint16_t fragmentChunkSize_ = 0;
  uint32_t touchedAt_ = 0;
  bool active_ = false;
};

} // namespace stomp::ble_ui
