# BLE screen UI manual check record

The BLE target and browser adapter have been built. Hardware checks for the BLE connection are still open.

## Current local checks

```sh
./scripts/check
./scripts/pio test -e native -f test_native_ble_ui
./scripts/pio run -e stompbridge-next-ble-ui
./scripts/pio run -e stompbridge-next-ui-dev
```

On 2026-11-18, `./scripts/check` passed three baseline guards, 14 Python host
tests, and 68 native Unity cases. Production, `stompbridge-next-input`, and
`stompbridge-next-usb` builds passed. The BLE UI group has 10 focused native
cases. The experimental BLE target passed its final compile with NimBLE-Arduino
2.5.2 (RAM 85,380 bytes; app 721,333 bytes). The final production UI build also
passed (RAM 54,104 bytes; app 1,327,421 of 1,376,256 bytes, 96.5%). The earlier
USB diagnostic build used 48,244 bytes RAM and 401,613 app bytes; the input
diagnostic used 18,732 bytes RAM and 294,173 app bytes. The USB UI development
target passed (RAM 76,632 bytes; app 485,617 bytes), and the browser scripts
passed `deno check`. These were build and static checks.

On 2026-11-19, the USB development target was flashed to the ESP32-S3 and
used with the local browser UI and a Valeton GP-5. The browser received repeated
snapshots and displayed the current patch, bank patch names, and effect model
names. The USB troubleshooting changes included a larger Arduino loop stack
and pacing between outgoing serial fragments.

## Hardware and browser follow-up

1. Host the static `ui/` files on a trusted HTTPS origin reachable over the
   screen's existing Wi-Fi. Confirm the browser URL is HTTPS before granting BLE
   permission. Do not serve HTML from GATT.
2. Deliberately flash only the experimental BLE UI target. Confirm its USB
   pedal session reaches verified synchronization. Leave the production image
   available as the recovery target.
3. Long-hold the utility switch to open the 60-second physical authorization
   window. Open the page in Mac Chrome, choose the StompBridge UI service,
   subscribe to notifications, negotiate v1, and request a fresh snapshot. Verify separate bridge and pedal status and certainty.
4. Select an absolute preset and set an effect state. Record request ID,
   admission response, operation phase, and the later pedal readback. Confirm a
   GATT write acknowledgement alone does not report success.
5. Change the pedal physically and verify a coherent snapshot update. Disconnect
   either link in turn; verify the other remains usable/visible, stale commands
   are canceled, and reconnect does not replay commands.
6. Repeat on Bluefy for iPhone and iPad, including screen lock, background,
   foreground, and reconnect. Record client version, OS version, ATT MTU,
   input-to-send latency, input-to-observed latency, snapshot latency, heap
   high-water mark, and queue/drop counters.
7. Verify the 60-second physical access expiry, revoke while connected, and
   reconnect behavior. Test negotiated encryption/bonding with both clients
   before recommending use beyond a supervised proof environment.
8. Qualify Linux separately with exact distro, browser build, adapter, and
   BlueZ versions. Do not claim ordinary Safari or Firefox support.

All device/client steps above remain unverified until hardware and the target
clients are available.
