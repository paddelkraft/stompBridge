# Development checks

Use Python 3.13 and run `./scripts/setup-dev` once in a fresh checkout. The
setup script refuses to replace an existing `.venv`. The project keeps
PlatformIO packages under `.platformio`; `dependencies.lock` pins ESP-IDF
components. The current checkout had a stale `.venv` launcher at baseline,
so initial validation used a disposable PlatformIO 6.2.0 environment. It was
then repaired with `python3 -m venv --upgrade .venv` and
`.venv/bin/python -m pip install -r requirements-dev.txt`.

Run `./scripts/check` for native GP-5 protocol fixtures, USB configuration
guards, Python host regressions, native Unity tests, and embedded compiles.
It does not upload or monitor hardware. Run individual stages with
`./scripts/check-gp5-baseline`, `./scripts/pio test -e native`, and
`./scripts/pio run -e esp32-s3-n16r8`.
The experimental BLE UI contract tests can be run alone with
`./scripts/pio test -e native -f test_native_ble_ui`. They cover framing, bounded
ingress, epochs/generations, JSON decoding/encoding, and result history. The
opt-in `./scripts/pio run -e stompbridge-next-ble-ui` build adds the USB pedal
application, encrypted NimBLE GATT server, physical authorization window, and
application-owned state/results. It does not upload or monitor hardware. The
The browser transports are isolated in `ui/ble_transport.js` and
`ui/serial_transport.js`; the screen renderer uses their shared event/command
boundary alongside legacy HTTP v1 and explicit simulation.
Web Bluetooth requires HTTPS and Chrome on macOS or Bluefy on iOS/iPad; client
interoperability still needs a supervised hardware check.

For local board-connected UI work without BLE or a deployed HTTPS origin:

```sh
./scripts/pio run -e stompbridge-next-ui-dev
./scripts/pio run -e stompbridge-next-ui-dev -t upload
./scripts/serve-ui-local
```

Open `http://localhost:8000` in Chromium, choose **USB DEV**, select the
StompBridge USB-to-UART service port, and grant serial access. Keep the pedal on
the separate USB host connector and close serial monitors before connecting.
The USB DEV control appears on localhost when Web Serial is available.
The serial adapter carries the same v1 frames; state snapshots, validation,
admission, and pedal operations remain on the ESP32. This development-only
mode bypasses BLE's physical authorization and encryption steps, tests the real
board application and UI, and does not validate BLE transport or clients.
The isolated `stompbridge-next-input` target compiles the new core and runs
only an input diagnostic if deliberately uploaded. The check script compiles
it; it does not upload any target.
The separate `stompbridge-next-usb` target compiles an experimental USB
application session. It automatically sends read-only sync requests after
USB descriptor identification and supports physical switch commands if
deliberately uploaded. The check script compiles it without uploading.
Its serial console reports `SYNC gen=N pending`, `confirmed preset=N` with
the device-specific `slot=` label and block values, `failed reason=...`, or
`interrupted by live command`. The confirmation line also reports whether
names and effect models are supported; unavailable values are not substituted
with made-up names. These
lines report device readback; successful MIDI transmission alone does not
produce `confirmed`.
After USB sync, a background request can report current effect model IDs and
`PATCH NAMES gen=N count=100`. The console prints the actual slot labels:
GP-5 `P00` through `P99`; Pocket Master `P01` through `P50` and `F01`
through `F50`.
The name list is printed only after every fragment and record index validates.
For sync diagnosis, this target also prints up to 20 bounded `SYNC RX` frame
summaries per USB attachment (`len`, first 16 MIDI bytes, last four bytes, and
the block bitmap window when present). Copy those lines together with the
`SYNC` result and the preceding USB identity line. Routine per-transfer USB
traffic is disabled in this target to reduce receive latency; host enumeration
diagnostics and USB errors remain visible.

Sustained USB traffic, repeated hot reconnects, and full GP-5 operation still
require physical checks. Capture
the full serial log from attachment through sync or error, the generated
`sdkconfig.esp32-s3-n16r8`, and the pedal display response. Avoid capturing
Wi-Fi credentials in shared logs.
