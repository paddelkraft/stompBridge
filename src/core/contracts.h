#pragma once

#include <cstddef>
#include <cstdint>

namespace stomp {

// All IDs are stable numeric values. A device plugin supplies their labels.
using BlockId = uint16_t;
using OperationId = uint32_t;
using SessionGeneration = uint32_t;

constexpr size_t kMaxBlocks = 16;
constexpr size_t kMaxPresets = 100;
constexpr size_t kMaxPresetName = 17;
constexpr size_t kMaxBlockLabel = 24;
constexpr size_t kMaxMidiMessage = 2048;
constexpr size_t kMidiIngressCapacity = 8;
constexpr size_t kControlIngressCapacity = 8;

enum class Certainty : uint8_t { Unknown, Assumed, Confirmed };
enum class SyncState : uint8_t { Idle, Pending, Confirmed, Failed, Interrupted };
enum class ConnectionPhase : uint8_t {
  Disconnected,
  Discovering,
  Identifying,
  Synchronizing,
  Ready,
  Degraded,
  Unsupported,
  FaultBackoff,
};
enum class IdentityKind : uint8_t {
  UsbDescriptor,
  BleVerifiedExchange,
  BleMidiServiceOnly,
};

struct DeviceIdentity {
  IdentityKind kind = IdentityKind::BleMidiServiceOnly;
  uint16_t vendorId = 0;
  uint16_t productId = 0;
  uint32_t verifiedToken = 0;
};
enum class CommandKind : uint8_t {
  SelectPreset,
  SetBlock,
  ToggleBlock,
  SetTuner,
  ToggleTuner,
  RequestReadback,
};
enum class Result : uint8_t {
  Accepted,
  Unsupported,
  NotReady,
  QueueFull,
  TransportFailed,
  TimedOut,
  Invalid,
  Interrupted,
};
enum class OperationPhase : uint8_t {
  Queued,
  Submitted,
  TransportComplete,
  Observed,
  Failed,
};

struct Command {
  CommandKind kind = CommandKind::RequestReadback;
  uint16_t target = 0; // Absolute preset index or stable block ID.
  bool enabled = false;
  OperationId operation = 0;
  SessionGeneration generation = 0;
};

struct BooleanValue {
  bool value = false;
  Certainty certainty = Certainty::Unknown;
};

struct PresetValue {
  uint16_t index = 0;
  Certainty certainty = Certainty::Unknown;
};

struct BlockCapability {
  BlockId id = 0;
  char label[kMaxBlockLabel] = {};
  bool readback = false;
};

struct DeviceCapabilities {
  uint16_t presetCount = 0;
  uint8_t blockCount = 0;
  bool presetReadback = false;
  bool tuner = false;
  bool tunerReadback = false;
  BlockCapability blocks[kMaxBlocks] = {};
};

struct MidiMessage {
  SessionGeneration generation = 0;
  uint16_t length = 0;
  uint8_t bytes[kMaxMidiMessage] = {};
};

enum class ControlEventKind : uint8_t {
  Attached,
  Disconnected,
  SendComplete,
  ReceiveLost,
};

struct ControlEvent {
  ControlEventKind kind = ControlEventKind::Disconnected;
  SessionGeneration generation = 0;
  OperationId operation = 0;
  bool success = false;
};

struct LogRecord {
  uint32_t atMs = 0;
  uint16_t code = 0;
  uint8_t level = 0;
  uint8_t component = 0;
  SessionGeneration generation = 0;
  OperationId operation = 0;
};

class Clock {
public:
  virtual ~Clock() = default;
  virtual uint32_t nowMs() const = 0;
};

class Logger {
public:
  virtual ~Logger() = default;
  virtual void record(const LogRecord &record) = 0;
};

} // namespace stomp
