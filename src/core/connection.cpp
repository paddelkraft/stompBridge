#include "connection.h"

namespace stomp {

bool ConnectionManager::matches(const DeviceDescriptor &descriptor,
                                const DeviceIdentity &identity) {
  if (identity.kind == IdentityKind::BleMidiServiceOnly ||
      descriptor.identityKind != identity.kind) return false;
  if (identity.kind == IdentityKind::UsbDescriptor)
    return descriptor.vendorId == identity.vendorId &&
           descriptor.productId == identity.productId;
  return identity.verifiedToken != 0 &&
         descriptor.verifiedToken == identity.verifiedToken;
}

void ConnectionManager::discover() {
  commands_.clear();
  device_ = nullptr;
  phase_ = ConnectionPhase::Discovering;
  retryCount_ = 0;
  retryPending_ = false;
}

void ConnectionManager::attach(const DeviceIdentity &identity) {
  ++generation_;
  if (generation_ == 0) ++generation_;
  commands_.clear();
  device_ = nullptr;
  phase_ = ConnectionPhase::Identifying;
  size_t found = 0;
  for (size_t i = 0; i < registryCount_; ++i) {
    if (matches(registry_[i], identity)) {
      device_ = &registry_[i];
      ++found;
    }
  }
  if (found != 1) {
    device_ = nullptr;
    phase_ = ConnectionPhase::Unsupported;
    return;
  }
  phase_ = ConnectionPhase::Synchronizing;
}

void ConnectionManager::resynchronize(SessionGeneration generation) {
  if (generation != generation_) {
    ++staleEvents_;
    return;
  }
  if (phase_ == ConnectionPhase::Ready || phase_ == ConnectionPhase::Degraded) {
    phase_ = ConnectionPhase::Synchronizing;
  }
}

void ConnectionManager::synchronized(SessionGeneration generation,
                                     bool verified, uint32_t /*now*/) {
  if (generation != generation_) {
    ++staleEvents_;
    return;
  }
  if (phase_ != ConnectionPhase::Synchronizing &&
      !(phase_ == ConnectionPhase::Ready && !verified)) return;
  if (verified) {
    phase_ = ConnectionPhase::Ready;
    retryCount_ = 0;
    retryPending_ = false;
  } else {
    // Readback failure does not imply that the MIDI connection failed.
    phase_ = ConnectionPhase::Degraded;
  }
}

void ConnectionManager::disconnected(SessionGeneration generation) {
  if (generation != generation_) {
    ++staleEvents_;
    return;
  }
  commands_.clear();
  device_ = nullptr;
  phase_ = ConnectionPhase::Disconnected;
  retryCount_ = 0;
  retryPending_ = false;
}

void ConnectionManager::fail(uint32_t now) {
  commands_.clear();
  device_ = nullptr;
  phase_ = ConnectionPhase::FaultBackoff;
  if (retryCount_ < 5) {
    retryAt_ = now + (250u << retryCount_);
    ++retryCount_;
    retryPending_ = true;
  } else {
    retryPending_ = false;
  }
}

void ConnectionManager::transportFault(SessionGeneration generation,
                                       uint32_t now) {
  if (generation != generation_) {
    ++staleEvents_;
    return;
  }
  fail(now);
}

void ConnectionManager::tick(uint32_t now) {
  if (phase_ == ConnectionPhase::FaultBackoff && retryPending_ &&
      int32_t(now - retryAt_) >= 0) {
    phase_ = ConnectionPhase::Discovering;
    retryPending_ = false;
  }
}

Result ConnectionManager::admit(Command command) {
  if (device_ == nullptr || (phase_ != ConnectionPhase::Ready &&
      phase_ != ConnectionPhase::Synchronizing &&
      phase_ != ConnectionPhase::Degraded)) return Result::NotReady;
  if (command.generation != generation_) return Result::NotReady;
  return commands_.push(command) ? Result::Accepted : Result::QueueFull;
}

} // namespace stomp
