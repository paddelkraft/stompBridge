#include <unity.h>

#include "devices/registry.h"

using namespace stomp;

struct FakePort : MidiSendPort {
  bool submit(const uint8_t *, size_t) override { return true; }
};
struct FakeOutput : OutputObserver {
  void outputEvent(const OutputEvent &) override {}
};
struct FakeUpdates : DeviceUpdateSink {
  void publish(const DeviceUpdate &) override {}
  void synchronizationStarted(SessionGeneration) override {}
  void synchronization(SessionGeneration, bool, Result) override {}
};

void test_registry_has_only_verified_usb_descriptors() {
  size_t count = 0;
  const DeviceDescriptor *entries = devices::registry(count);
  TEST_ASSERT_EQUAL_UINT32(2, count);
  TEST_ASSERT_EQUAL(IdentityKind::UsbDescriptor, entries[0].identityKind);
  TEST_ASSERT_EQUAL_HEX16(0x0284, entries[0].productId);
  TEST_ASSERT_EQUAL(PluginKind::PocketMaster, entries[0].plugin);
  TEST_ASSERT_EQUAL(IdentityKind::UsbDescriptor, entries[1].identityKind);
  TEST_ASSERT_EQUAL_HEX16(0x0184, entries[1].productId);
  TEST_ASSERT_EQUAL(PluginKind::Gp5, entries[1].plugin);
}

void test_plugin_instances_are_destroyed_on_replacement() {
  FakePort port;
  FakeOutput observer;
  FakeUpdates updates;
  SessionOutput output(port, observer);
  devices::PluginStorage storage;
  DevicePlugin *first = storage.create(PluginKind::PocketMaster, output, updates);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_EQUAL_STRING("pocket_master", first->id());
  storage.destroy();
  TEST_ASSERT_NULL(storage.get());
  DevicePlugin *second = storage.create(PluginKind::Gp5, output, updates);
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_EQUAL_STRING("valeton_gp5", second->id());
  TEST_ASSERT_EQUAL_UINT16(100, second->capabilities().presetCount);
  storage.destroy();
  TEST_ASSERT_NULL(storage.get());
  TEST_ASSERT_NULL(storage.create(static_cast<PluginKind>(99), output, updates));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_registry_has_only_verified_usb_descriptors);
  RUN_TEST(test_plugin_instances_are_destroyed_on_replacement);
  return UNITY_END();
}
