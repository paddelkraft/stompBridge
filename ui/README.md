# Phone screen over Bluetooth

StompBridge uses a static web UI as its display and touch interface. Bluefy
loads the page over HTTPS and talks directly to the ESP32 over Web Bluetooth.
The effects pedal stays connected to the ESP32 by USB. The web host serves the
page files. Preset and effect commands travel over the local BLE link.

## Connect from iPhone

1. Flash `stompbridge-next-ble-ui` through the USB-to-UART service port.
   Connect the powered pedal to the USB host port and check for a confirmed
   `SYNC` line. See the [hardware guide](../docs/hardware.md).
2. Install Bluefy and open the POC page inside it with Bluetooth enabled.
   [Bluefy on the App Store](https://apps.apple.com/us/app/bluefy-web-ble-browser/id1492822055) ·
   [Hosted StompBridge UI](https://paddelkraft.se/stompbridge/)
3. Hold **UTILITY** until the bridge opens its 60-second authorization window.
   Tap **CONNECT BLE** in the page, select **StompBridge UI**, and accept the
   pairing prompt if one appears. Disconnect any other GATT client before
   connecting. The bridge requires encryption and bonding.
4. Wait for the current preset and effect states to appear. Change a preset
   using the board and watch the screen, then change a preset or effect from
   the screen and watch the pedal and the UI's state. **Activity** shows
   connection and operation details.
5. The 60-second window is for establishing the connection. An encrypted,
   bonded session can stay open after it expires. Holding **UTILITY** again
   revokes access. After a disconnect, open a fresh window to reconnect.

Live control with Bluefy and a GP-5 was demonstrated on iPhone. The serial
trace confirmed encryption, bonding, and retention past the 60-second
deadline. Long-run use, physical revoke/reconnect, screen wake lock, and
effect-model labels on the phone still need checks. Phone trials with the
Pocket Master and on iPad are open. See the
[manual check record](../docs/ble-ui-manual-checks.md).

## Host your own copy

Serve the contents of `ui/` from a static host with a trusted HTTPS certificate.
Keep the HTML, CSS, and JavaScript together so the relative asset URLs resolve.
No backend or build step is required for the phone UI. Use the matching project
version for firmware and page assets.

## Develop locally

Run `./scripts/serve-ui-local` and open <http://localhost:8000> in a desktop
browser. **USE SIMULATOR** runs an explicit standalone simulation. For the real
board over Chromium Web Serial, flash `stompbridge-next-ui-dev`, close the
serial monitor, and choose **USB DEV**. Select the service port and leave the
pedal on the separate host port. The same screen logic and UI frames are used
in both BLE and USB development. See the [development guide](../docs/development.md).

The desktop BLE path uses **CONNECT BLE** with the BLE firmware and a browser
that exposes Web Bluetooth. Brave/Linux discovery was unsuccessful in the
recorded trials. The desktop Chrome path still needs hardware qualification.
The earlier Wi-Fi path is described under [earlier prototypes](../docs/legacy-prototypes.md).
