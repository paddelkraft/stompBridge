# Verified support baseline

This records evidence available in the committed `9f770a1` firmware and
documentation. A successful build or protocol test does not establish device
reliability. The replacement must preserve the known-good firmware until
physical acceptance.

| Pedal | Transport | Evidence | Remaining limit |
| --- | --- | --- | --- |
| Pocket Master | USB host | Combined firmware opened MIDI interface 3, OUT `03`, IN `82`; four preset switches worked. Host testing confirmed presets and Drive/Delay/Reverb readback. | Tuner is locally assumed; full reconnect and sustained traffic validation remain. |
| GP-5 | USB host | The earlier combined firmware selected P02-P04 but timed out during sync. The experimental `stompbridge-next-usb` target confirmed preset and Drive/Delay/Reverb readback across fresh boots and a pedal reconnect. It decoded all 100 preset names from command `0x6A`; P00–P02 match the pedal screen, allowing for the screen's added space in `Great Pedal`. Current Drive/Delay/Reverb model IDs were also decoded on hardware. | Sustained traffic, repeated hot reconnects, and physical input timing still need validation. The default combined firmware retains its earlier sync limitations. |
| Pocket Master | BLE MIDI | BLE central and service discovery code exists. | No reliable automatic pedal identity supplied to plugin selection; no verified embedded pedal session. |
| GP-5 | BLE MIDI | BLE transport contains GP-5 framing comments. | No reliable automatic identity or verified embedded session. |

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
