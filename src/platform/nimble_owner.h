#pragma once

namespace stomp::platform {

// Owns the singleton NimBLE-Arduino initialization shared by peripheral and
// central transports. Call once from the firmware composition task.
bool beginNimble(const char *deviceName);

} // namespace stomp::platform
