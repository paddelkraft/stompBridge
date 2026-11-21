# BLE screen UI manual check record

Bluefy on iPhone exercised live control with a USB-connected GP-5. The
bridge logged that an encrypted, bonded session stayed connected past the
60-second authorization deadline. The remaining phone and desktop checks are
listed below. For setup, use the [phone screen guide](../ui/README.md).

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

A 2026-11-19 desktop trial passed all 10 focused native tests and verified
that the loopback server on port 8000 serves the workspace UI. The experimental
BLE image built and flashed on `/dev/ttyACM0`. The board read GP-5 patch and
effect names over USB. Host BlueZ initially found the custom service UUID
without a device name. After moving the name to the scan response and
reflashing, an active LE scan found **StompBridge UI** with service UUID
`9e0d0001-7f4a-4b86-9c31-6a8b7d4c2000`. Brave 156.1.98.63 opened the local
URL. In the initial Brave session, the UI reported that Web Bluetooth was
unavailable before the chooser, while Linux Bluetooth settings still listed
**StompBridge UI**. With the flags enabled, Brave opened the chooser but
listed no compatible devices with the advertised UUID or exact-name fallback.
An active BlueZ LE scan still saw the bridge at −40 dBm. The chooser now uses
the full advertised name as a prefix so Chromium can apply the match after
native discovery. The subsequent GATT connection still requires the custom
service. The prefix fallback also produced an empty chooser. In a temporary
Brave profile, an unfiltered chooser listed 10 nearby devices but omitted
the bridge. Clearing the bridge's unpaired BlueZ entry did not change that
result. BlueZ rediscovered it with the correct UUID and −39 dBm signal.
Brave/Linux discovery is unresolved. Use Mac Chrome on loopback for the next
desktop browser comparison. The user confirmed that LightBlue on iPhone
discovered the same bridge. The next phone check is to connect during the
physical authorization window and read protocol info. Phone pairing,
notifications, commands, and reconnect remain open.

LightBlue later showed the bridge as disconnected with Connect disabled. The
firmware could stop advertising after rejecting an unauthorized connection.
The updated image now restarts advertising after each disconnect. After
flashing, BlueZ connected with the authorization window closed, the bridge
terminated the link, and a fresh scan rediscovered the name and service UUID
after clearing the unpaired cache entry. Retry LightBlue and Brave against
this image.

LightBlue then showed a connectable advertisement, but the link did not stay
up. The serial trace showed the physical window opening before the bridge
terminated the connection. The firmware required a bonded link but had
configured NimBLE with bonding off. The experimental BLE UI now requests
bonding and logs the security result. After flashing, the LightBlue retry
produced `BLE UI security encrypted=1 bonded=1` in the bridge serial log.
LightBlue also read protocol-info characteristic `...2001` and showed
`7B 22 76 22 3A 31`, the UTF-8 prefix `{"v":1`. The full value and epoch
were not captured. After disconnecting LightBlue, the user retried Brave
with the updated firmware and the chooser remained empty. An isolated Brave
profile reproduced it: the unfiltered chooser listed seven devices, none of
them the bridge, and the UI filter showed zero. The next browser comparison
is Mac Chrome on loopback or Bluefy on a trusted HTTPS origin.

On 2026-11-20, Bluefy loaded and controlled the bridge before the firmware
closed the session at the 60-second deadline. The updated image keeps an
encrypted, bonded session open after the connection window expires. In the
Bluefy retry the serial log confirmed `encrypted=1 bonded=1` and then
`BLE UI access window expired; session retained`. A preset change after
the deadline and physical revocation remain to be checked.

Bluefy showed generic effect labels on initial connection. On the bridge,
GP-5 preset P05 read back Overdrive, Analog Delay, and Plate. The hosted UI
used the same `effect_name` field as the firmware serializer. A desktop
GATT probe did not establish a paired session. During a subsequent Bluefy
connection, the bridge logged snapshot revision 115 with GP-5 P17 model
names Overdrive, Analog Delay, and Shimmer. The phone still showed generic
names. The next UI build uses the live model as the effect-card heading and
shows the received names and certainty in Activity. The hosted version still
needs to be updated and checked on the phone.

## Hardware and browser follow-up

1. On the desktop running the browser, start `./scripts/serve-ui-local` and
   open <http://localhost:8000/>. In DevTools check `window.isSecureContext`
   and `Boolean(navigator.bluetooth)`. On Linux, verify the powered adapter
   and check `chrome://flags/#enable-experimental-web-platform-features`
   if the API is missing. In Brave on Linux, check both
   `brave://flags/#brave-web-bluetooth-api` and
   `brave://flags/#enable-experimental-web-platform-features`. Relaunch after
   changing flags, then recheck API availability. Connection failures are
   displayed in the Activity panel.
2. Deliberately flash only the experimental BLE UI target. Confirm its USB
   pedal session reaches verified synchronization. Leave the production image
   available as the recovery target.
3. Long-hold the utility switch to open the 60-second physical authorization
   window. Click **CONNECT BLE**, choose the StompBridge UI service,
   subscribe to notifications, negotiate v1, and request a fresh snapshot. Verify separate bridge and pedal status and certainty.
4. Select an absolute preset and set an effect state. Record request ID,
   admission response, operation phase, and the later pedal readback. Confirm a
   GATT write acknowledgement alone does not report success.
5. Change the pedal physically and verify a coherent snapshot update. Disconnect
   either link in turn; verify the other remains usable/visible, stale commands
   are canceled, and reconnect does not replay commands.
6. Open the [hosted StompBridge UI](https://paddelkraft.se/stompbridge/) in
   Bluefy for iPhone and iPad. On 2026-11-20, the hosted HTML and its referenced
   CSS and JavaScript matched the workspace over trusted HTTPS. Use this
   deployment on the managed phone, which blocks local certificate profiles.
   Disconnect LightBlue, open the physical authorization window, and test the
   first snapshot, live controls, screen lock, foreground recovery, and
   reconnect. Record client and OS versions, ATT MTU, latencies, heap
   high-water mark, and queue/drop counters.
7. Verify an established session still controls the pedal after 60 seconds.
   Confirm that the expired window blocks new connections, and test physical
   revoke while connected and reconnect behavior. Test negotiated
   encryption and bonding with both clients
   before recommending use beyond a supervised proof environment.
8. Qualify Linux separately with exact distro, browser build, adapter, and
   BlueZ versions. Do not claim ordinary Safari or Firefox support.

Record browser GATT results here as each stage is verified.
