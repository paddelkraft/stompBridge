#include <unity.h>

#include "core/connection.h"

using namespace stomp;

constexpr DeviceDescriptor devices[] = {
    {"pocket_master", IdentityKind::UsbDescriptor, 0x84ef, 0x0284, 0},
    {"valeton_gp5", IdentityKind::UsbDescriptor, 0x84ef, 0x0184, 0},
};

void test_verified_usb_attach_allows_live_commands_during_sync() {
  ConnectionManager manager(devices, 2);
  manager.discover();
  manager.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0184, 0});
  TEST_ASSERT_EQUAL(ConnectionPhase::Synchronizing, manager.phase());
  const auto generation = manager.generation();
  Command command{CommandKind::SelectPreset, 6, false, 1, generation};
  TEST_ASSERT_EQUAL(Result::Accepted, manager.admit(command));
  manager.synchronized(generation, true, 100);
  TEST_ASSERT_EQUAL(ConnectionPhase::Ready, manager.phase());
  TEST_ASSERT_EQUAL(Result::Accepted, manager.admit(command));
  manager.resynchronize(generation);
  TEST_ASSERT_EQUAL(ConnectionPhase::Synchronizing, manager.phase());
  TEST_ASSERT_EQUAL_UINT32(2, manager.pendingCommands());
  TEST_ASSERT_EQUAL(Result::Accepted, manager.admit(command));
  manager.synchronized(generation, true, 110);
  manager.disconnected(generation);
  TEST_ASSERT_EQUAL_UINT32(0, manager.pendingCommands());
  TEST_ASSERT_EQUAL(Result::NotReady, manager.admit(command));
}

void test_service_only_and_ambiguous_identity_are_unsupported() {
  ConnectionManager manager(devices, 2);
  manager.attach({IdentityKind::BleMidiServiceOnly, 0, 0, 0});
  TEST_ASSERT_EQUAL(ConnectionPhase::Unsupported, manager.phase());
  manager.attach({IdentityKind::UsbDescriptor, 0x1111, 0x2222, 0});
  TEST_ASSERT_EQUAL(ConnectionPhase::Unsupported, manager.phase());
  DeviceDescriptor ambiguous[] = {devices[0], devices[0]};
  ConnectionManager duplicate(ambiguous, 2);
  duplicate.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0284, 0});
  TEST_ASSERT_EQUAL(ConnectionPhase::Unsupported, duplicate.phase());
}

void test_failed_sync_keeps_connection_and_rejects_stale_callback() {
  ConnectionManager manager(devices, 2);
  manager.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0184, 0});
  const auto first = manager.generation();
  manager.synchronized(first, false, 100);
  TEST_ASSERT_EQUAL(ConnectionPhase::Degraded, manager.phase());
  TEST_ASSERT_EQUAL(Result::Accepted,
                    manager.admit({CommandKind::SelectPreset, 6, false, 1, first}));
  manager.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0184, 0});
  const auto second = manager.generation();
  TEST_ASSERT_TRUE(second != first);
  manager.synchronized(first, true, 400);
  TEST_ASSERT_EQUAL(ConnectionPhase::Synchronizing, manager.phase());
  TEST_ASSERT_EQUAL_UINT32(1, manager.staleEvents());
  manager.synchronized(second, true, 400);
  TEST_ASSERT_EQUAL(ConnectionPhase::Ready, manager.phase());
}

void test_queue_saturation_and_replacement_discard() {
  ConnectionManager manager(devices, 2);
  manager.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0284, 0});
  manager.synchronized(manager.generation(), true, 0);
  for (uint32_t i = 0; i < 16; ++i) {
    Command command{CommandKind::SelectPreset, uint16_t(i), false, i + 1,
                    manager.generation()};
    TEST_ASSERT_EQUAL(Result::Accepted, manager.admit(command));
  }
  Command extra{CommandKind::SelectPreset, 17, false, 17, manager.generation()};
  TEST_ASSERT_EQUAL(Result::QueueFull, manager.admit(extra));
  manager.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0184, 0});
  TEST_ASSERT_EQUAL_UINT32(0, manager.pendingCommands());
  TEST_ASSERT_EQUAL(Result::NotReady, manager.admit(extra));
}

void test_retry_exhaustion_stays_faulted() {
  ConnectionManager manager(devices, 2);
  manager.discover();
  uint32_t now = 0;
  for (uint8_t attempt = 0; attempt < 6; ++attempt) {
    manager.attach({IdentityKind::UsbDescriptor, 0x84ef, 0x0184, 0});
    manager.transportFault(manager.generation(), now);
    TEST_ASSERT_EQUAL(ConnectionPhase::FaultBackoff, manager.phase());
    if (attempt < 5) {
      now += 250u << attempt;
      manager.tick(now);
      TEST_ASSERT_EQUAL(ConnectionPhase::Discovering, manager.phase());
    }
  }
  manager.tick(now + 100000);
  TEST_ASSERT_EQUAL(ConnectionPhase::FaultBackoff, manager.phase());
  TEST_ASSERT_EQUAL_UINT8(5, manager.retryCount());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_verified_usb_attach_allows_live_commands_during_sync);
  RUN_TEST(test_service_only_and_ambiguous_identity_are_unsupported);
  RUN_TEST(test_failed_sync_keeps_connection_and_rejects_stale_callback);
  RUN_TEST(test_queue_saturation_and_replacement_discard);
  RUN_TEST(test_retry_exhaustion_stays_faulted);
  return UNITY_END();
}
