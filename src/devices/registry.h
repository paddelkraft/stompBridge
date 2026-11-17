#pragma once

#include "core/connection.h"
#include "core/device_plugin.h"

namespace stomp::devices {

const DeviceDescriptor *registry(size_t &count);
const DeviceDescriptor *registry();
size_t registryCount();

// One compile-time plugin instance at a time, constructed in fixed storage
// for each session and destroyed during teardown.
class PluginStorage {
public:
  ~PluginStorage() { destroy(); }
  DevicePlugin *create(PluginKind kind, SessionOutput &output,
                       DeviceUpdateSink &sink);
  void destroy();
  DevicePlugin *get() const { return plugin_; }

private:
  static constexpr size_t kStorageBytes = 3072;
  alignas(std::max_align_t) uint8_t storage_[kStorageBytes] = {};
  DevicePlugin *plugin_ = nullptr;
  PluginKind kind_ = PluginKind::PocketMaster;
};

} // namespace stomp::devices
