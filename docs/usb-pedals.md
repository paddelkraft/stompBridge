# Standalone USB MIDI for Pocket Master and GP-5

The ESP32-S3 is the USB host. The pedal plugs into the controller's USB-A
host port. A computer on the separate USB-to-UART service port is optional
during normal operation and useful for flashing and serial logs.

## Wiring and power

| USB-A host signal | Connection |
| --- | --- |
| VBUS | Controller's externally supplied 5 V host rail |
| GND | Common ground with the ESP32 and the external supply |
| D− | ESP32-S3 GPIO19 |
| D+ | ESP32-S3 GPIO20 |

Use a known data cable. On the tested board, the computer on the service
port powered the ESP32 but did not provide usable VBUS at the USB-A port.
The powerbank supplied the host rail through the separate power connection.
The Pocket Master could run on its battery and still needed host VBUS to
present its USB connection. Its charging indication helped confirm that
host power had reached it.

Measure VBUS against GND at the USB-A port with the pedal connected if
enumeration fails. This setup read about 5.5 V unloaded and 5.43 V loaded.
Those are observations, not target voltages. A multimeter reading can miss
brief dips. Continuity checks can confirm pin mapping and shorts with power
off, but do not verify the USB signals during enumeration.

## Firmware and build

Use the default `esp32-s3-n16r8` environment. It builds Arduino as an
ESP-IDF component so the USB host library is compiled with
[`sdkconfig.defaults`](../sdkconfig.defaults). The earlier Arduino-only
build linked a precompiled USB library with 30 ms reset recovery and no
custom hub power-on delay. Editing the project defaults alone could not
change those precompiled settings.

| Setting | Value in the generated plugin build |
| --- | --- |
| USB hubs and multilevel hubs | Enabled |
| Control transfer buffer | 512 bytes |
| Downstream hub power-on delay | 250 ms |
| Root port debounce | 250 ms |
| Root port reset hold | 50 ms |
| Root and downstream reset recovery | 100 ms |
| Set-address recovery | 20 ms |
| Downstream reset attempts | 1 (component default; confirmed in generated configs) |

Check the generated `sdkconfig.esp32-s3-n16r8` after changing build settings.
The control transfer buffer serves USB enumeration and is separate from the
MIDI transfer and SysEx buffers.

The reset-attempt symbol is hidden in the component's Kconfig and defaults to
one. Project defaults cannot override it reliably. Startup now prints the
compiled macro values; use that line and the generated sdkconfig as evidence.
The earlier successful hub enumeration was observed with the generated value
at one, so the project uses that confirmed component default.

```sh
./scripts/pio run -e esp32-s3-n16r8
./scripts/pio run -e esp32-s3-n16r8 -t upload
./scripts/pio device monitor -b 115200
```

Close any serial monitor before uploading and select the USB-to-UART port
explicitly if several ports are present. The working build used Arduino
3.3.12, ESP-IDF 5.5.5, and the `espressif/usb` component 1.6.1. If PlatformIO
reports `HTTPClientError` while installing `toolchain-esp32ulp`, check that
the build process has network access and a writable PlatformIO cache. That
resolved the download here. The `usb-hub-timing` environment is a separate
ESP-IDF diagnostic that temporarily replaces the controller app if flashed.

## Bring-up and troubleshooting

1. Supply host VBUS, power on the pedal, and connect it to the controller's
   USB-A port. Monitor the ESP32 service port at 115200 baud.
2. Look for `USB-MIDI host ready`, an attach event, then `USB-MIDI connected`
   and `Selected pedal plugin`. The firmware matches VID/PID automatically.

   | Pedal | VID:PID |
   | --- | --- |
   | Sonicake Pocket Master | `84EF:0284` |
   | Valeton GP-5 | `84EF:0184` |

3. In the first bank, press B, then A, then D. Check the pedal display for
   P02, P01, then P04. The Pocket Master sends preset commands as CC1 with
   values 2, 1, and 4. The GP-5 uses its own SysEx preset commands.

| Serial symptom | What to check |
| --- | --- |
| Host ready, no attach | Measure host VBUS and verify the cable is in the USB-A host port. |
| Repeated root attach/detach or `Root port reset failed` | Check power and the data path, then capture the full sequence. One root reset failed during a successful Pocket Master startup and the host subsequently enumerated it. |
| Root port status changes, then no external-port event | Root attachment is visible in `USB root port` logs. Check host VBUS and whether the pedal's internal hub appears in the `HUB`/`EXT_HUB` logs. |
| `EXT_PORT` reset or `ENUM` short-device-descriptor error | This is downstream hub enumeration. The first short descriptor transaction occurs before the full device/configuration descriptors, MIDI interface claim, or MIDI traffic. Check VBUS and the compiled hub timing values. |
| `USB full device descriptor` appears but configuration retrieval fails | Enumeration passed the device descriptor stage; investigate configuration descriptor parsing and size. The USB control transfer maximum is 512 bytes. |
| MIDI interface claim succeeds but no preset changes | Enumeration and interface claim completed. Check MIDI traffic and the selected pedal plugin/protocol. The 512-byte SysEx receive buffer only affects MIDI responses after the interface is active. |
| MIDI connected, one switch appears dead | Try another switch, then hold the suspect switch and look for `PRESSED` and `RELEASED`. A is GPIO9. The input needs 25 ms of stable contact. A preset log shows successful submission, so check the pedal display for the result. |

For a controlled serial capture, boot the controller with its service cable
connected and the host powerbank supplying VBUS. Wait for `USB-MIDI host
ready` before connecting or power-cycling the pedal. Capture from the first
root-port transition through either `USB-MIDI connected` or a repeated
`ENUM`/`EXT_PORT` failure. The host library's `USBH`, `HUB`, `EXT_HUB`,
`EXT_PORT`, and `ENUM` debug logs distinguish attachment, external-hub setup,
descriptor stages, and downstream reset recovery.

For a quick recovery, keep host VBUS supplied, reconnect the pedal, and
watch for plugin selection. If it still fails, reset the controller with the
powered pedal attached and capture the startup log. A controller reset with
the Pocket Master connected did restore enumeration during testing.

## Hardware validation

- **Pocket Master:** The standalone diagnostic reached the pedal's internal
  hub and sent a preset change once host VBUS was present. The combined
  firmware later opened MIDI interface 3 (OUT `0x03`, IN `0x82`) and all
  four preset switches worked. A briefly appeared unreliable, then worked
  with a longer press and subsequently with normal presses.
- **GP-5:** Earlier standalone trials reported intermittent hangs. On the
  newly flashed combined build, one live session selected P02, P03, and P04
  from switches B, C, and D, and the pedal display followed the commands.
  Serial showed the GP-5 plugin issuing preset and state-request messages.
  This confirms one end-to-end session; cold starts, hot reconnects, effects,
  and repeated operation still need validation before calling it reliable.
  A later reset completed hub enumeration, claimed interface 3 (OUT `0x03`,
  IN `0x82`), and exchanged MIDI data, but then the IN transfer resubmit
  failed with `ESP_ERR_INVALID_STATE` and the plugin sync timed out. The
  status was an IN transfer overflow. The firmware now requests 1024 bytes
  per receive transfer and flushes/clears the endpoint after receive errors.
  In the next live capture, those recovery calls succeeded and receive
  traffic resumed without the resubmit error, but overflow events continued
  and GP-5 sync still timed out. The logged GP-5 response command IDs also do
  not match the IDs the plugin currently handles, so response-to-command
  mapping and stable sync remain open.
  In a later experimental USB build, the pedal was reconnected while serial
  capture ran, but downstream port 3 repeatedly failed at
  `CHECK_SHORT_DEV_DESC`. No MIDI interface or readback frame was available in
  that capture. Increasing downstream reset recovery from 100 to 300 ms did
  not change the failure, so the experiment was reverted. Confirm host VBUS
  under load and the pedal's power/cable path before attributing that capture
  to a protocol decoder.
  Subsequent experimental captures with host enumeration logging restored
  opened interface 3. A fixed preset-response CRC check was corrected; GP-5
  CRC varies with preset index. The USB adapter's eight-event queue overflowed
  during the patch burst. A larger queue then exposed a second overflow where
  the adapter copied all frames into the application's eight-message ingress
  before draining it. The experimental target now uses a bounded 32-event
  platform queue and pumps application ingress after each event. It confirmed
  preset and effect state on three fresh controller boots, after a pedal
  reconnect, and after adding patch-fragment CRC validation. No receive loss
  appeared in those successful captures. Extended traffic and repeated hot
  reconnects remain to be tested.
