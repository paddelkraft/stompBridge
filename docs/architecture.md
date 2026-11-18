# Replacement architecture under construction

The default production firmware still builds from the existing `src/main.cpp`
and plugin paths. `stompbridge-next-input` is a separate, switch-only
diagnostic build that compiles the new core for the ESP32-S3. It sends no pedal
commands and serves no UI. `stompbridge-next-usb` is a separate experimental
USB session build. It automatically identifies a USB pedal by its descriptor,
sends read-only synchronization requests, and routes board switches through
the new application. It has no UI. MIDI output, GP-5 synchronization, effect
models, and the 100-preset name list have hardware captures.
This keeps known-good firmware available during the replacement.

## Ownership and contracts

The application task will own connection phase, session generation, device
plugins, operation scheduling, and `ReadModel`. Platform callbacks will copy
MIDI into `OwnedIngress` and enqueue control events. The platform must protect
producer calls with a short critical section; `OwnedIngress` itself is a
single-context queue and is not thread-safe. Control events are drained before
MIDI. A disconnect clears pending MIDI and controls and closes that generation.
Overflow clears the MIDI queue and exposes `ReceiveLost`, requiring parser
reset and resynchronization. A later generation rejects stale callbacks.

`src/core/contracts.h` defines semantic commands and typed results. `Accepted`
means admitted; it does not mean transport completion or observed pedal state.
`Certainty::Confirmed` requires verified device feedback. Submission or
transport completion may establish `Assumed` only. The read model increments
its revision when an observable value changes and invalidates certainty on
disconnect. Local bank selection remains separate from pedal-observed preset.
`planToggle` requests readback for an unknown value when supported. If readback
is unavailable it plans ON. A readback timeout is a failure; it does not
silently plan ON.
`SessionOutput` accepts bounded owned messages, reports queue admission,
submission, transport completion, and failure separately, and applies
plugin-provided pacing. Live commands take priority over queued readback, and
two queue slots are reserved from background and readback work for live sends.
Transport completion releases the send buffer; it is not a pedal receipt.
A send failure or completion timeout stops that session's remaining output.
The transport must copy submitted bytes until its completion callback. Only
verified device feedback may mark an operation observed.

Current contract limits are 100 presets, 16 blocks, 24-byte block labels,
17-byte preset names, 2048-byte owned MIDI messages, eight MIDI ingress slots,
eight control ingress slots, and 16 gesture events. These are initial bounds
based on the committed GP-5 SysEx assembly size plus headroom. Physical
traffic and heap/stack measurements must validate them before cutover.
Preset slot labels follow device capabilities: Pocket Master P01–P50 and
F01–F50, GP-5 P00–P99. Slot labels do not claim preset-name feedback.

`Switching` accepts numeric switch IDs, raw samples, and monotonic millisecond
timestamps. It has no GPIO, MIDI, device, or browser dependencies. The
application binds its typed events to semantic commands. Default timings
are 25 ms debounce, 500 ms hold, and 60 ms chord window. A chord waits for its
window, chooses the largest completed mask, and uses explicit priority for
equal-size overlaps. Immediate bindings emit at debounce completion.

## Next boundaries

The native `ConnectionManager` now checks unambiguous USB descriptor identity,
rejects BLE-MIDI service-only identity, admits live commands during readback,
and discards pending commands on loss or replacement. A readback failure leaves
an identified MIDI session in `Degraded`, while a transport fault uses bounded
retries.
Native Pocket Master and GP-5 plugin instances now use a typed update sink and
the session output queue. Pocket Master requires matching preset reads around
26 ordered live-state fragments; GP-5 requires matching preset reads around an
exact host-shaped patch-state fragment. Both stay unready on timeout, malformed
responses, or a changed preset; live commands remain available. Live control bytes are fixture-tested. State
becomes assumed only after transport completion, and readback is scheduled for
preset and block changes. Tuner state remains assumed because reliable tuner
feedback is unavailable. GP-5 preset, block, effect-model, and 100-name
readback are verified on the ESP32-S3 experimental target. Pocket Master
preset/block readback and the 100-name directory are also verified there.
Its live-state model IDs are decoded separately from the directory, and
current P01 Drive, Delay, and Reverb labels were checked against its screen.
The compile-time registry currently contains only the two verified USB
VID/PID identities. It constructs one plugin at a time in fixed storage and
destroys it on replacement. BLE-MIDI service discovery alone cannot select a
plugin.

The native `Application` is the sole owner of connection state, plugin
instances, output scheduling, and the read model. It dispatches supported
live commands before or after synchronization. A queued live command waits
for the plugin's previous send buffer to complete rather than being dropped.
It drops old-generation input after replacement,
invalidates certainty on receive loss, and applies confirmed preset readback
to the local four-preset bank. The GPIO adapter is wired in the experimental
USB target; the browser adapter remains open. A readback-first unknown toggle is held until confirmed state arrives;
its failure remains a typed operation result.
After a receive fault, queued or late MIDI is not dispatched to the plugin
until a new synchronization session is established.

The USB experimental target scans ten GPIO switches before each USB poll.
Current bindings use immediate press after debounce: bank down/up remain
local; Drive, Delay, Reverb, and Tuner use toggles; A-D select absolute presets
from the local bank. Unknown toggles follow their own readback policy.
The USB target owns a bounded 32-event FreeRTOS queue. Host-task
callbacks copy complete MIDI messages or completion/loss notices into it; the
application loop drains each event into `Application` and pumps it before the
next event. A full queue gives the application task up to 25 ms to drain it;
remaining overflow faults the current
session. Transmit completion carries the generation recorded at submission.
The adapter still relies on the existing USB transport's SysEx assembly and
has hardware-confirmed GP-5 preset and block readback across several boots
and a reconnect. Two queued USB IN transfers captured all 106 fragments of
the GP-5 and Pocket Master 100-preset name responses. The serial console now
reports sync start, confirmation with observed preset and block values, effect
models, the complete preset-name list, or a
failure reason. In-place retry after a synchronization fault and
rapid same-descriptor reconnect detection remain open.
Pocket Master hot unplug now releases the claimed MIDI interface before
closing its device handle. The earlier root recovery abort was caused by three
idle pipes left behind when close failed. Two hardware detach/reattach cycles
recovered the root hub without reboot, then confirmed state and names in new
session generations. Longer and faster reconnect sequences remain unverified.

Verified BLE identity exchanges, BLE-MIDI framing,
and `/api/v2` serialization are not implemented. The existing USB driver now exposes optional transmit-completion
and receive-loss callbacks for the future application adapter; its hub,
framing, and endpoint recovery paths are unchanged. The BLE driver has not
been modified. A native USB-MIDI event decoder assembles complete SysEx,
passes channel messages and interleaved realtime events, and discards partial
state after receive loss. It is not yet wired to the ESP-IDF USB callback.
GP-5 readback now has captured hardware evidence in the experimental target;
the replacement will not enter Ready after an unverified timeout.
