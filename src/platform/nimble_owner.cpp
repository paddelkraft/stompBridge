#include "nimble_owner.h"

#include <NimBLEDevice.h>

namespace stomp::platform {
namespace {
bool initialized = false;
}

bool beginNimble(const char *deviceName, bool bonding) {
  if (initialized) return true;
  if (!NimBLEDevice::init(deviceName)) return false;
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(bonding, false, true);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  initialized = true;
  return true;
}

} // namespace stomp::platform
