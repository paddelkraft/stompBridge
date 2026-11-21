# Verified support baseline

This records proof-of-concept evidence through 2026-11-20. The original
`esp32-s3-n16r8` firmware remains the PlatformIO default. The two-way USB
results below come from the replacement USB core. The BLE screen firmware
builds on that core. See the [POC walkthrough](../README.md) for the target
to flash.

| Component | Transport | Evidence | Remaining limit |
| --- | --- | --- | --- |
| Pocket Master | USB host | Combined firmware opened MIDI interface 3, OUT `03`, IN `82`; four preset switches worked. Host testing confirmed presets and Drive/Delay/Reverb readback. The experimental `stompbridge-next-usb` target identified `84EF:0284`, confirmed P01 after switch A and P04 after D, then confirmed Drive, Delay, and Reverb transitions to off. A 2026-11-17 controller capture decoded all 100 names from the `12 40` directory; current P01 Drive, Delay, and Reverb IDs matched the pedal-screen labels Scream, Analog, and Plate 2. Earlier two hot reconnects passed. A 2026-11-18 cross-pedal capture observed Pocket Master detection, state/model/name sync after swaps with the GP-5, and ended with Pocket Master synced again, without controller reboot. Each unplug logged an IN receive error and failed the departing session's sync before cleanup. | Tuner is locally assumed. Long-run and rapid reconnects remain unverified. Other effect-name catalog entries are mapped but not individually checked on this pedal. |
| GP-5 | USB host | The earlier combined firmware selected P02-P04 but timed out during sync. The experimental `stompbridge-next-usb` target confirmed preset and Drive/Delay/Reverb readback across fresh boots and a pedal reconnect. It decoded all 100 preset names from command `0x6A`; P00–P02 match the pedal screen, allowing for the screen's added space in `Great Pedal`. Current Drive/Delay/Reverb model IDs were also decoded on hardware. The 2026-11-18 cross-pedal capture observed GP-5 detection, state/model/name sync after swaps with Pocket Master, without controller reboot. Each unplug logged an IN receive error and failed the departing session's sync before cleanup. | Sustained traffic, rapid reconnects, and physical input timing still need validation. The default combined firmware retains its earlier sync limitations. |
| Pocket Master | BLE MIDI | BLE central and service discovery code exists. | No reliable automatic pedal identity supplied to plugin selection; no verified embedded pedal session. |
| GP-5 | BLE MIDI | BLE transport contains GP-5 framing comments. | No reliable automatic identity or verified embedded session. |
| Screen UI | BLE GATT | Bluefy on iPhone controlled the bridge with a GP-5 attached. The bridge logged an encrypted, bonded session retained past the 60-second authorization deadline. | Post-deadline commands, physical revocation, reconnect, wake lock, and phone effect model labels need verification. Pocket Master and iPad screen trials remain open. Brave/Linux did not find the bridge in its chooser. Mac Chrome remains unverified. |
| Screen UI | USB dev serial | A local browser connected over Web Serial to the ESP32-S3 with a GP-5 attached. It received repeated snapshots showing the current patch, bank names, and effect model names. | Live commands and reconnect behavior still need checks on this path. |

The USB host build enables hub and multilevel hub support, 512-byte control
transfers, 250 ms hub power-on and root debounce, 50 ms root reset hold,
100 ms root/downstream recovery, and 20 ms set-address recovery. The committed
driver queues two 4096-byte IN transfers, assembles up to 2048 bytes of SysEx,
and flushes and clears the IN endpoint after receive errors. The generated
configuration and hardware logs must be checked when changing these values.

The command `0x48` response observed on hardware uses 72 fragments and contains
model-like strings, including `14 DST` and `Force OCD`. This is not the patch
list shown on the GP-5 display. The actual preset list comes from the
sync-start request and command `0x6A`: 106 fragments containing a two-byte
header and 100 indexed, 20-byte records. A single queued 4096-byte IN transfer
repeatedly missed fragments 64 and 65 of the earlier `0x48` dump at its buffer
boundary; two queued transfers produced the complete response and later the
complete 100-preset list. Preset names are published only after all fragments
and record indices validate.

The default combined firmware has its earlier timeout behavior. The
experimental replacement allows independent live MIDI commands during
readback and reports confirmed state only after verified responses. No
response-ID reinterpretation is authorized without captured frames and
hardware evidence.

The first Pocket Master hot-unplug capture exposed repeated USB IN transfer
errors. The transport now pauses and rate-limits receive retries. A later
instrumented capture found `hcd_port_recover()` failing with three idle HCD
pipes after the upstream hub disappeared. The disconnect callback had called
`usb_host_device_close()` without first releasing the claimed MIDI interface;
that close returned `ESP_ERR_INVALID_STATE` and left the three pipes allocated.
The transport now releases the interface, retries cleanup if transfers are
still pending, and closes the device only after release. Two hardware
unplug/reconnect cycles then recovered the root port without reboot and
completed fresh Pocket Master sync and name reads. The temporary USB library
instrumentation was removed after diagnosis.

Pocket Master directory readback uses the read-only `12 40` request described
in the [Pocket Master Interoperability Guide](https://github.com/klotznick/pocket-master-interoperability-guide).
The controller capture on 2026-11-17 validated all 106 fragments and 100
indexed records; P01 was `piranhas`, F01 was `PoketMastr`, and F50 was
`Bass Q`. Drive, Delay, and Reverb effect IDs came from the current live
buffer's `0x3003` field. Their labels use the public
[Pocket Master model map](https://github.com/voronkovd/pocket-master-mcp/blob/main/src/pocket_master_mcp/data/device_map.json).
The pedal screen confirmed the current P01 labels Scream, Analog, and Plate 2.
Other catalog entries still need individual hardware checks.
