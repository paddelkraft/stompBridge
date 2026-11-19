#include "ui_ble_frames.h"

#include <cstring>

namespace stomp::ble_ui {
namespace {
constexpr uint8_t kMagic0 = 0x53;
constexpr uint8_t kMagic1 = 0x42;
}

size_t fragmentCount(size_t length, size_t attPayload) {
  if (length > kMaxMessage || attPayload <= kHeaderSize ||
      attPayload > kMaxAttPayload) return 0;
  const size_t chunk = attPayload - kHeaderSize;
  return length == 0 ? 1 : (length + chunk - 1) / chunk;
}

FrameError encodeFragment(MessageType type, uint16_t transfer,
                          const uint8_t *message, size_t messageLength,
                          size_t index, size_t attPayload, Frame &out) {
  out = {};
  if ((message == nullptr && messageLength != 0) ||
      messageLength > kMaxMessage || attPayload < kHeaderSize ||
      attPayload > kMaxAttPayload) return FrameError::TooLarge;
  const size_t count = fragmentCount(messageLength, attPayload);
  if (count == 0 || count > kMaxFragments || index >= count) return FrameError::TooLarge;
  const size_t chunk = attPayload - kHeaderSize;
  const size_t offset = index * chunk;
  const size_t dataLength = messageLength > offset
      ? (messageLength - offset < chunk ? messageLength - offset : chunk) : 0;
  out.bytes[0] = kMagic0;
  out.bytes[1] = kMagic1;
  out.bytes[2] = kProtocolVersion;
  out.bytes[3] = static_cast<uint8_t>(type);
  out.bytes[4] = static_cast<uint8_t>(transfer);
  out.bytes[5] = static_cast<uint8_t>(transfer >> 8);
  out.bytes[6] = static_cast<uint8_t>(index);
  out.bytes[7] = static_cast<uint8_t>(index >> 8);
  out.bytes[8] = static_cast<uint8_t>(count);
  out.bytes[9] = static_cast<uint8_t>(count >> 8);
  out.bytes[10] = static_cast<uint8_t>(messageLength);
  out.bytes[11] = static_cast<uint8_t>(messageLength >> 8);
  if (dataLength) std::memcpy(out.bytes + kHeaderSize, message + offset, dataLength);
  out.length = static_cast<uint16_t>(kHeaderSize + dataLength);
  return FrameError::None;
}

FrameError Reassembler::accept(const uint8_t *frame, size_t length,
                               size_t attPayload, uint32_t nowMs,
                               Message &completed) {
  if (active_ && static_cast<uint32_t>(nowMs - touchedAt_) > kFragmentTimeoutMs) {
    reset();
    return FrameError::Timeout;
  }
  if (frame == nullptr || length < kHeaderSize || length > attPayload ||
      attPayload < kHeaderSize || attPayload > kMaxAttPayload ||
      frame[0] != kMagic0 || frame[1] != kMagic1 ||
      frame[3] < static_cast<uint8_t>(MessageType::Hello) ||
      frame[3] > static_cast<uint8_t>(MessageType::Resync)) {
    reset();
    return FrameError::Malformed;
  }
  if (frame[2] != kProtocolVersion) {
    reset();
    return FrameError::VersionMismatch;
  }
  const auto type = static_cast<MessageType>(frame[3]);
  const uint16_t transfer = static_cast<uint16_t>(frame[4] | (frame[5] << 8));
  const uint16_t index = static_cast<uint16_t>(frame[6] | (frame[7] << 8));
  const uint16_t total = static_cast<uint16_t>(frame[8] | (frame[9] << 8));
  const uint16_t expectedLength = static_cast<uint16_t>(frame[10] | (frame[11] << 8));
  const size_t dataLength = length - kHeaderSize;
  if (total == 0 || index >= total || expectedLength > kMaxMessage) {
    reset();
    return FrameError::Malformed;
  }
  if (!active_) {
    if (index != 0) return FrameError::OutOfOrder;
    if (total == 1) {
      if (dataLength != expectedLength) {
        reset();
        return FrameError::Malformed;
      }
      fragmentChunkSize_ = static_cast<uint16_t>(dataLength);
    } else {
      if (dataLength == 0 || dataLength > expectedLength ||
          total != (expectedLength + dataLength - 1) / dataLength) {
        reset();
        return FrameError::Malformed;
      }
      fragmentChunkSize_ = static_cast<uint16_t>(dataLength);
    }
    pending_.length = 0;
    pending_.type = type;
    pending_.transfer = transfer;
    fragmentTotal_ = total;
    expectedLength_ = expectedLength;
    nextIndex_ = 0;
    active_ = true;
  }
  if (type != pending_.type || transfer != pending_.transfer || total != fragmentTotal_ ||
      expectedLength != expectedLength_) {
    reset();
    return FrameError::Malformed;
  }
  if (index < nextIndex_) {
    reset();
    return FrameError::Duplicate;
  }
  if (index != nextIndex_) {
    reset();
    return FrameError::OutOfOrder;
  }
  const size_t expectedChunk = index + 1 < total ? fragmentChunkSize_
      : expectedLength_ - pending_.length;
  if (dataLength != expectedChunk || pending_.length + dataLength > expectedLength_) {
    reset();
    return FrameError::Malformed;
  }
  if (dataLength) std::memcpy(pending_.bytes + pending_.length, frame + kHeaderSize, dataLength);
  pending_.length = static_cast<uint16_t>(pending_.length + dataLength);
  ++nextIndex_;
  touchedAt_ = nowMs;
  if (nextIndex_ != fragmentTotal_) return FrameError::Incomplete;
  completed.type = pending_.type;
  completed.transfer = pending_.transfer;
  completed.length = pending_.length;
  std::memcpy(completed.bytes, pending_.bytes, completed.length);
  reset();
  return FrameError::Complete;
}

void Reassembler::reset() {
  pending_.length = 0;
  nextIndex_ = 0;
  fragmentTotal_ = 0;
  expectedLength_ = 0;
  fragmentChunkSize_ = 0;
  touchedAt_ = 0;
  active_ = false;
}

} // namespace stomp::ble_ui
