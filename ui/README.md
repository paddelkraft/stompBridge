# StompBridge HTML UI prototype

This is a dependency-free controller UI prototype. Simulation is opt-in with
the **USE SIMULATOR** button; an unavailable firmware API stays offline and
does not create simulated command success. The prototype simulates device
selection, bank and preset navigation, effect toggles, tuner mode, state
certainty, and an activity log.

Serve the UI on the computer running the browser:

```sh
./scripts/serve-ui-local
```

Then visit <http://localhost:8000>. Chromium treats loopback HTTP as a secure
context. For BLE on Linux, verify that `navigator.bluetooth` is present and
that the Bluetooth adapter is powered. Chromium's Experimental Web Platform
features flag may be required.

Keyboard shortcuts: `1`/`2`/`3` toggle Drive, Delay, and Reverb; `T` opens the
tuner; `A`–`D` select presets; Left/Right changes bank.

The **CONNECT BLE** button uses `ble_transport.js` to connect to the experimental
GATT service, negotiate v1, and request a fresh state snapshot. **USB DEV**
uses `serial_transport.js` and Web Serial to connect locally to the opt-in
`stompbridge-next-ui-dev` firmware over the board's USB-to-UART service port.
It carries the same framed UI protocol, while state, command validation, and
pedal operations stay on the ESP32. Pending commands are shown beside the
current patch, and lost/late results are surfaced as unknown outcomes followed
by a fresh state request; commands are never replayed automatically. For the desktop BLE test, flash `stompbridge-next-ble-ui` and
keep the pedal on the USB host connector. For the later phone test, host the
static `ui/` assets on trusted HTTPS while the phone uses its usual network.
The physical 60-second authorization window must be opened on the bridge
with a utility-switch long hold before a writable session. This path is a proof
implementation: Chrome on macOS and Bluefy on iOS/iPad still need supervised
hardware validation of bonding, encryption, notifications, and reconnects.

The USB DEV button appears only on localhost in a secure context with Web
Serial support. For the USB development path, run `./scripts/serve-ui-local`, open
<http://localhost:8000> in Chromium, choose **USB DEV**, select the board's
USB-to-UART port, and grant access. Keep the pedal connected to the separate
USB host connector. Close any serial monitor first. This exercises the real
board application and screen without testing BLE radio/client behavior.

When opened from the ESP32 at <http://192.168.4.1/>, the UI retains its legacy
HTTP v1 state/action adapter. Use **USE SIMULATOR** for an explicit local
simulation; failed BLE or HTTP connections never fall back to simulated state.

PlatformIO runs `scripts/embed_web_ui.py` before each firmware build to gzip
these files into `include/web_ui_assets.h`; no separate filesystem upload is
needed.

During an active BLE session, the UI requests a screen wake lock where the
browser supports it. Check **Activity** for the result. The lock releases on
disconnect or when the page is hidden. Returning to the visible page requests
it again if BLE is still connected.
