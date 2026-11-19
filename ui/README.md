# StompBridge HTML UI prototype

This is a dependency-free controller UI prototype. Simulation is opt-in with
the **USE SIMULATOR** button; an unavailable firmware API stays offline and
does not create simulated command success. The prototype simulates device
selection, bank and preset navigation, effect toggles, tuner mode, state
certainty, and an activity log.

Open `index.html` directly, or serve the folder locally:

```sh
python3 -m http.server 8080 --directory ui
```

Then visit <http://localhost:8080>. Localhost is treated as a secure context
for browser development.

Keyboard shortcuts: `1`/`2`/`3` toggle Drive, Delay, and Reverb; `T` opens the
tuner; `A`–`D` select presets; Left/Right changes bank.

The **CONNECT BLE** button uses `ble_transport.js` to connect to the experimental
GATT service, negotiate v1, and request a fresh state snapshot. **USB DEV**
uses `serial_transport.js` and Web Serial to connect locally to the opt-in
`stompbridge-next-ui-dev` firmware over the board's USB-to-UART service port.
It carries the same framed UI protocol, while state, command validation, and
pedal operations stay on the ESP32. Pending commands are shown beside the
current patch, and lost/late results are surfaced as unknown outcomes followed
by a fresh state request; commands are never replayed automatically. Serve the static `ui/` assets from HTTPS
while the screen stays on its normal Wi-Fi connection; do not load the page from
BLE. The physical 60-second authorization window must be opened on the bridge
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
