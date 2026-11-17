# Prompt: Pocket Master and GP-5 feature parity

```text
Continue StompBridge's replacement architecture toward functional parity
between the Sonicake Pocket Master and Valeton GP-5 on USB. Read
README.md, docs/support-matrix.md, docs/architecture.md,
docs/development.md, docs/gp5-patch-readout-2026-11-17.md, and
todo/architecture-replacement-plan.md, then inspect the current code and
working tree. Treat documentation as evidence to verify against code and
hardware, not as proof that every feature is complete.

Implement the next concrete parity stage, not just a plan. Start with a
feature matrix for both device plugins and the default versus experimental
firmware. Cover identification, connection and reconnect, outgoing preset and
effect commands, bank and physical switch behavior, current-preset and block
readback, preset names, effect model names, tuner, sync status, error reporting,
and web UI. Mark each cell implemented, hardware verified, unverified, or
unsupported, with a source or test for each claim.

Use the shared application contracts and capability model to implement the
missing behavior that each pedal actually supports. Keep one active pedal and
automatic verified device identification. Preserve the known-good default
firmware until the replacement passes acceptance. Do not make Pocket Master
pretend to support GP-5-only data, or mark a value confirmed without verified
feedback. If Pocket Master preset names or effect model names are available,
find and validate their actual protocol responses before exposing them. Show
unknown or unsupported clearly where the pedal does not supply a value.

Performance priority: while a verified USB-MIDI connection is usable, send
independent live MIDI commands promptly. A missing readback response or pedal
receipt must not block later independent commands. Protocol pacing and the
transport's actual send-buffer lifetime still apply. Keep optional name,
model, and UI synchronization below live commands in priority. Preserve
readback-first behavior only for commands whose meaning depends on unknown
state, such as toggles. Review SessionOutput's timeout/failure path against
this rule, and distinguish transport failure from a missing state response.

For GP-5, preserve the hardware-verified 100-preset name response: sync-start
request, command 0x6A, 106 CRC-checked fragments, a two-byte header, and 100
indexed records. P00-P02 contain the USB strings GreatPedal, Neo Soul, and
Star Night; the pedal screen displays Great Pedal with a space. Command 0x48
returns a model-like catalog and must not be labeled as presets. Preserve the
two queued USB IN transfers that made the complete response reliable. Keep
current effect model IDs separate from preset names.

Add focused tests for shared behavior, device-specific decoding, stale session
callbacks, incomplete or corrupt readback, and live-command priority during
sync failure. Run ./scripts/check. If the pedals and controller are available,
validate both devices on the experimental USB target: boot, send several live
commands, read state, disconnect/reconnect, and confirm the names and states
against each pedal's display. Record exactly which hardware checks ran; leave
unavailable checks explicitly unverified. Update the support matrix, relevant
architecture notes, and the parity matrix with the result.

Finish with implemented changes, test and hardware evidence, remaining gaps,
and the next smallest stage toward equal user experience. Do not claim BLE
parity from USB results. Do not commit unless I ask.
```
