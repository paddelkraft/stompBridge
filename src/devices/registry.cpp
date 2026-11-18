#include "registry.h"

#include "devices/gp5/plugin.h"
#include "devices/pocket_master/plugin.h"

#include <new>

namespace stomp::devices {

namespace {
constexpr DeviceDescriptor kDevices[] = {
    {"pocket_master", IdentityKind::UsbDescriptor, 0x84ef, 0x0284, 0,
     PluginKind::PocketMaster},
    {"valeton_gp5", IdentityKind::UsbDescriptor, 0x84ef, 0x0184, 0,
     PluginKind::Gp5},
};
static_assert(sizeof(pocket::Plugin) <= 3072);
static_assert(sizeof(gp5::Plugin) <= 3072);
} // namespace

const DeviceDescriptor *registry(size_t &count) {
  count = sizeof(kDevices) / sizeof(kDevices[0]);
  return kDevices;
}

const DeviceDescriptor *registry() { return kDevices; }

size_t registryCount() { return sizeof(kDevices) / sizeof(kDevices[0]); }

DevicePlugin *PluginStorage::create(PluginKind kind, SessionOutput &output,
                                    DeviceUpdateSink &sink) {
  destroy();
  kind_ = kind;
  if (kind == PluginKind::PocketMaster)
    plugin_ = new (storage_) pocket::Plugin(output, sink);
  else if (kind == PluginKind::Gp5)
    plugin_ = new (storage_) gp5::Plugin(output, sink);
  return plugin_;
}

void PluginStorage::destroy() {
  if (plugin_ == nullptr) return;
  plugin_->stop();
  if (kind_ == PluginKind::PocketMaster)
    static_cast<pocket::Plugin *>(plugin_)->~Plugin();
  else
    static_cast<gp5::Plugin *>(plugin_)->~Plugin();
  plugin_ = nullptr;
}

} // namespace stomp::devices
