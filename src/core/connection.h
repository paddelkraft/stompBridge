#pragma once

#include "bounded_queue.h"
#include "contracts.h"

namespace stomp {

constexpr size_t kApplicationCommandQueueCapacity = 16;

enum class PluginKind : uint8_t { PocketMaster, Gp5 };

struct DeviceDescriptor {
  const char *id = nullptr;
  IdentityKind identityKind = IdentityKind::UsbDescriptor;
  uint16_t vendorId = 0;
  uint16_t productId = 0;
  uint32_t verifiedToken = 0;
  PluginKind plugin = PluginKind::PocketMaster;
};

class ConnectionManager {
public:
  ConnectionManager(const DeviceDescriptor *registry, size_t registryCount)
      : registry_(registry), registryCount_(registryCount) {}

  void discover();
  void attach(const DeviceIdentity &identity);
  void resynchronize(SessionGeneration generation);
  void synchronized(SessionGeneration generation, bool verified, uint32_t now);
  void disconnected(SessionGeneration generation);
  void transportFault(SessionGeneration generation, uint32_t now);
  void tick(uint32_t now);
  Result admit(Command command);
  bool next(Command &command) { return commands_.pop(command); }

  ConnectionPhase phase() const { return phase_; }
  SessionGeneration generation() const { return generation_; }
  const DeviceDescriptor *device() const { return device_; }
  size_t pendingCommands() const { return commands_.size(); }
  uint32_t staleEvents() const { return staleEvents_; }
  uint8_t retryCount() const { return retryCount_; }

private:
  void fail(uint32_t now);
  static bool matches(const DeviceDescriptor &descriptor,
                      const DeviceIdentity &identity);

  const DeviceDescriptor *registry_;
  size_t registryCount_;
  const DeviceDescriptor *device_ = nullptr;
  BoundedQueue<Command, kApplicationCommandQueueCapacity> commands_;
  ConnectionPhase phase_ = ConnectionPhase::Disconnected;
  SessionGeneration generation_ = 0;
  uint32_t retryAt_ = 0;
  uint32_t staleEvents_ = 0;
  uint8_t retryCount_ = 0;
  bool retryPending_ = false;
};

} // namespace stomp
