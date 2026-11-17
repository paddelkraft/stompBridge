# StompBridge replacement architecture plan

Status: replacement foundation in progress. On 2026-11-17 the user authorized
proceeding from commit `9f770a1`, while retaining its documented GP-5
connectivity limitations. Keep the existing firmware as the hardware fallback.

Progress: support matrix refreshed in [docs/support-matrix.md](../docs/support-matrix.md).
Native GP-5 request/decoder fixtures, USB configuration and recovery guards,
and existing Python host tests pass. The initial native PlatformIO/Unity target
passes two protocol tests. The unchanged firmware compiled successfully with
PlatformIO 6.2.0, Arduino 3.3.12, and ESP-IDF 5.5.5; it was not flashed.
Stage 1 baseline capture is complete from available repository evidence.
At the latest native check, 52 Unity cases passed with warnings treated as
errors; three USB baseline guards and 14 Python host tests also passed. The
documented check entry point compiled the existing firmware successfully.
Stage 2 has a pinned PlatformIO bootstrap, a check entry point, and a CI
workflow, but formatting/static checks and browser contract checks remain.
Stage 3 has native-tested typed contracts, bounded owned ingress, and a read
model. The unknown-toggle policy plans readback first and reports timeout
without a fallback command. Live commands remain admissible during sync and
after readback failure; the output scheduler prioritizes them over queued
readback. Successive live commands remain pending until a plugin can accept
them after transport completion. Task-safe callback adapters and API v2 remain.
Stage 4 has a native-tested pure switching subsystem and GPIO routing in the
experimental USB target; physical timing validation remains. Stage 5 has a native session-state
skeleton with verified USB descriptor selection, separate dispatch and sync
health, stale generation
rejection, command discard, and capped retry tests; actual transport adapters,
plugin sessions, and BLE framing remain open. A pure USB-MIDI event decoder now
has native tests but is not connected to the low-level USB driver. A bounded
session output scheduler distinguishes admission, submission, completion, and
timeout, with plugin-supplied pacing. An isolated `stompbridge-next-input`
firmware target compiles the core and runs only switch diagnostics. It compiled
for the ESP32-S3 without flashing; the default target remains the known-good
application and was recompiled after the CMake split. Neither target was
flashed. The existing USB transport now has optional transmit-completion and
receive-loss notifications. A separate `stompbridge-next-usb` build wires those
signals and copied MIDI into the native application through a bounded
FreeRTOS queue; it sends automatic read-only synchronization requests and
supports switch-driven live commands through the application.
The user reports MIDI output working on hardware. The experimental console
prints sync start, confirmation with observed state, interruption, failure
reason, and bounded received-frame summaries. GP-5 preset response CRC now
varies correctly with the preset, and the patch-state fragment is CRC-checked.
The experimental USB path now holds a complete GP-5 readback burst in a bounded
32-event queue and drains application ingress incrementally. It confirmed
preset and three effect states on three fresh controller boots, after one
pedal reconnect, and on a final boot with both CRC checks in place. No receive
loss appeared in those successful captures. A prior `CHECK_SHORT_DEV_DESC`
failure persisted under a 300 ms reset-recovery experiment, which was reverted
to 100 ms; enumeration later succeeded with host debug logging restored.
Its USB path still uses the legacy transport's SysEx assembly. Repeated hot
reconnects, sustained traffic, and in-place retry after a sync fault remain.
Stage 6 now has native Pocket Master and GP-5 read-only sync state
machines and live-control byte fixtures. Both require verified responses and
never confirm state after timeout. The GP-5 replacement readback path now has
hardware-confirmed preset and effect state; sustained reliability and the
legacy plugin's response-ID discrepancy remain open. A fixed-storage registry has only the two verified
USB identities. Runtime plugin composition and physical acceptance remain
open. Stage 7 has a native application owner with integration tests for
identity, verified sync, live command admission during degraded sync, state publication, stale session
events, receive loss, local bank behavior, and physical switch bindings.
The web adapter and the
capability-driven UI remain open. Stage 8 remains open. See
[docs/architecture.md](../docs/architecture.md) for the current boundaries.

Resume with [architecture-replacement-prompt.md](./architecture-replacement-prompt.md).

## Goal and scope

Design a replacement architecture for the ESP32-S3 firmware and its embedded web UI. Keep the existing implementation as behavioral and protocol evidence, not as the structure to preserve.

The replacement separates physical switching, application behavior, connection lifecycle, device protocols, generic transports, optional device-specific transport adapters, UI presentation, and diagnostics.

Confirmed scope:

- One active pedal session at a time, using USB or BLE.
- Compile-time plugins for Pocket Master and Valeton GP-5; easy addition of future devices.
- Physical input includes debounce, tap, hold, and configured multi-switch chords.
- Per-binding gesture policy: ordinary controls can act immediately; exclusive tap/hold/chord bindings defer recognition appropriately.
- Initial timing defaults: 25 ms debounce, 500 ms hold, 60 ms chord window. These are configuration values subject to hardware validation, not scattered literals.
- No double-tap, repeat, runtime-loaded third-party plugins, multi-pedal routing, or persistent preset/editor writes in the initial scope.
- Keep the Python host as a reference and diagnostic tool, not a second replacement implementation.
- Require fully automatic device identification. Do not provide manual profile overrides or infer identity from the standard BLE-MIDI service.
- Reject device commands while disconnected; discard queued commands on disconnect; resynchronize before declaring a session ready.
- Performance priority: once a MIDI transport is connected and the pedal is reliably identified, supported live commands should be sent without waiting for a receipt, readback, or confirmation of an earlier command. Synchronization and UI state are secondary to timely outgoing MIDI. A required protocol pacing interval still applies; it must not become an acknowledgement barrier.
- Keep transport availability, command dispatch, synchronization health, and state certainty distinct. A sync timeout or missing response must be visible to the user and diagnostics, but must not globally stop independent live commands while the MIDI connection remains usable. Commands whose meaning depends on unknown state, such as a toggle, may wait for their own readback under the agreed unknown-toggle policy.
- Unknown toggle state triggers readback first where supported. If readback is unavailable, the first toggle requests ON and remains explicitly assumed.

## Review of the current implementation

These observations describe the working tree when the plan was drafted. GP-5 connectivity work takes priority and may supersede them. Revalidate this section against the latest code, tests, documentation, and landed connectivity changes before implementation.

2026-11-17 review: the earlier extensive dirty tree is stale; only the
untracked `todo/` directory remained before work began. The GP-5 USB recovery
and plugin code are committed, but `docs/usb-pedals.md` still records overflow,
unknown response IDs, and sync timeout. BLE identity remains unavailable to
the current plugin selector. The replacement requires verified sync before
reporting confirmed pedal state; live command dispatch remains available on a
usable, identified MIDI connection. Do not promote either transport to verified GP-5
support based on code or compilation alone.

2026-11-17 behavior decision: confirmed preset readback updates the
controller's local A-D bank to the corresponding four-preset page. Local bank
navigation remains independently available while disconnected.

Evidence comes from the working tree, which already contains extensive modified and untracked files. Do not revert, clean, or overwrite those changes during implementation without agreement.

Useful foundations:

- `include/midi_transport.h` establishes a transport boundary.
- `include/pedal_plugin.h`, `src/pedal_plugins.cpp`, and the two plugin implementations already isolate some device logic.
- Physical input and browser actions already share a bounded action queue.
- State certainty distinguishes unknown, assumed, and confirmed.
- The toolchain wrapper isolates PlatformIO packages and disables telemetry.
- The firmware already embeds the dependency-free UI.

Problems the replacement must address:

1. `src/main.cpp` owns debounce, physical mappings, action queues, connection selection, plugin lifecycle, JSON construction, and application scheduling.
2. `PedalAction` encodes today's fixed controls; `PedalState` fixes four preset slots and Drive/Delay/Reverb/Tuner fields. These are not sufficient device-agnostic contracts.
3. Plugins currently own local bank navigation as well as pedal state. Local controller selection and observed device state need separate owners.
4. USB and BLE callbacks deliver directly into plugin handling, while the application loop also polls plugins and reads their state. The new design needs explicit task ownership and copied message lifetimes.
5. USB receive handling currently assembles SysEx rather than providing a complete, general MIDI message contract. BLE receive handling strips two leading bytes rather than implementing a complete BLE-MIDI decoder.
6. Plugin selection requires USB VID/PID. BLE does not currently supply an identity through that interface, so advertised BLE connectivity is not evidence of working plugin selection.
7. Direct serial logging is spread across modules. Logging format is also consumed by the Python host's switch parser, so changing diagnostic output can break a real integration.
8. A successful send submission is not equivalent to transport completion or confirmed device state.
9. `docs/usb-pedals.md` records unresolved GP-5 synchronization, response-ID, and receive-overflow behavior. Pocket Master embedded handling also lacks the host implementation's richer readback. A rewrite must not invent protocol certainty.
10. Tests shown in the firmware tree primarily cover pins; no tracked CI workflow or shared architecture/developer guidance was found.

## Proposed architecture

```text
GPIO adapter -> debounce -> gesture recognizer -> InputEvent
                                                   |
                                                   v
Web action adapter --------------------------> Application
                                             /     |      \
                                    bindings   controller   ConnectionManager
                                      /          state             |
                              semantic command                 DeviceSession
                                                                  |
                                                        DevicePlugin
                                                                  |
                                                   optional device adapter
                                                                  |
                                                   generic MIDI transport
                                                          USB / BLE

Transport callbacks -> bounded owned ingress -> application event pump
DevicePlugin state updates -> application-owned read model -> web serializer
All boundaries -> injected structured logger -> bounded serial sink
```

Use explicit typed queues and narrow interfaces. Do not introduce a global event bus, service locator, dependency-injection framework, or a class for every message.

### 1. Switching subsystem

- The GPIO adapter reads physical pins and timestamps edges. Board configuration supplies stable numeric switch IDs; labels are presentation only.
- Pure C++ debounce and gesture recognition consume sampled input and an injected monotonic clock.
- Emit typed events containing gesture kind, participating switch IDs, and recognition timestamp. No MIDI, plugin references, bank logic, or UI calls.
- The application owns the mapping from gestures to semantic commands.
- Bindings declare immediate-press or deferred-exclusive recognition. Reject conflicting bindings at configuration validation rather than firing ambiguous combinations.
- For deferred bindings, hold consumes tap; a recognized chord consumes its member singles/holds for that activation. Require release before rearming. Prefer the largest configured chord, then explicit deterministic priority for equal-size overlaps.
- A partial chord candidate falls back to its configured single-switch gesture when the recognition window closes. A held incomplete chord must still have deterministic hold behavior.
- Startup-held switches must be released before generating actions. Test timer wraparound and independent switches.
- Gesture machinery runs regardless of pedal connection; the application decides whether the resulting command is local or requires a ready session.

### 2. Application and controller state

- Own bindings, local bank/page selection, action admission, state revisions, and the published read model.
- Convert current A-D bindings into absolute preset commands using the selected local bank. Plugins never interpret physical switch labels.
- Keep local controller state separate from pedal-observed state.
- Browser commands and physical gestures converge on the same semantic command admission path. Browser clicks do not impersonate raw switch edges.
- Start with a small typed command set: select absolute preset, set block enabled, toggle block, set/toggle tuner, and request supported readback. Represent block targets with stable IDs.
- Return typed admission/execution results such as accepted, unsupported, not-connected, queue-full, transport-failed, and timed-out. An accepted HTTP request is not a successful pedal operation. Do not use missing state confirmation as a global command-admission gate.
- Keep queues bounded; no delayed replay of commands after disconnect or session replacement. Local bank navigation may remain available without a pedal.

### 3. ConnectionManager and DeviceSession

- ConnectionManager owns transport startup/stop, discovery orchestration, candidate selection, device identification, plugin selection, session creation, reconnect policy, and teardown.
- Model connection/identity and synchronization health separately. A connected, identified session may dispatch supported live commands while synchronization is pending or degraded; `ready` must state exactly which capability it describes.
- Transports retain their low-level SDK connection and resource responsibilities; the manager makes lifecycle decisions. Plugins own device initialization and synchronization, not transport discovery.
- A session bundles the identified peer, selected plugin, transport/adapter, operation scheduling, and a monotonically increasing generation ID.
- Stamp callback data, commands, and completions with the session generation. Drop stale results after disconnect or replacement and record a diagnostic counter.
- On loss, invalidate certainty, cancel pending work, reset gesture-to-device command backlog, release resources, and publish connection state before reconnect attempts.
- Start read-only synchronization after identification without holding the live-command path behind it. If sync fails, expose degraded state and unknown values; never claim confirmed state or full synchronization. Unsupported readback fields may remain unknown when the plugin explicitly declares that limitation.
- USB identification can use verified descriptor matches. BLE requires reliable identifying metadata or a verified, read-only identification exchange before plugin activation.
- Standard BLE-MIDI advertisement alone is insufficient. If no reliable identification method exists for a target, report it as unsupported and document that limitation rather than guessing.
- Retry transient connection/transport failures with capped backoff. Do not blindly replay a command that might already have executed.

### 4. Device plugins and protocol code

- Use a static registry of descriptors and plugin factories. Select only on an unambiguous supported identity; create/reset per-session instances rather than retaining stale singleton session state.
- Define a narrow plugin contract: descriptor/capabilities, start session, consume MIDI messages, consume semantic command, advance scheduled work, publish state updates, and stop session.
- Inject the session output channel, clock, and logger. Plugins must not include GPIO, web server, JSON, USB-host, BLE SDK, or serial APIs.
- Keep encoding/decoding pure and independently testable. Device-specific request ordering, synchronization, fragment assembly, and pacing remain in each plugin/protocol module.
- A bounded session scheduler distinguishes admission, submission, transport completion, and observed state. It dispatches independent live commands without waiting for prior completion or observed-state confirmation; plugins provide only evidenced protocol pacing and ordering requirements. Application code does not impose a universal 120 ms command gap.
- Publish capabilities for supported operations, preset ranges/labels, blocks, and readback support. Use bounded typed structures, not arbitrary string-keyed property bags.
- Publish device state updates; the application applies them to its read model. Plugins retain only the internal state necessary to implement their protocol.
- Transport success can establish an assumed value at most; only verified device feedback establishes confirmed state.
- Treat pending operations separately from observed values. Failed sends must not look like successful toggles. If readback is supported but times out, report the failure instead of silently taking the no-readback fallback.
- Keep unknown tuner state unknown until a command is actually sent; use the documented assumed-state policy where tuner feedback is unavailable.
- Preserve only verified live-control and read-only synchronization operations. Protocol discrepancies require fixtures and hardware evidence, not speculative decoders.

### 5. Generic and device-specific transports

Generic transports:

- Own USB enumeration/interfaces/endpoints or BLE discovery/GATT connectivity, standard MIDI framing, receive assembly, and send completion/error reporting.
- Expose a common complete-MIDI-message boundary with identity metadata and lifecycle events. Support channel messages, fragmented SysEx, and interleaved realtime messages according to the supported MIDI standard.
- Have no pedal CC tables, preset semantics, vendor request IDs, or plugin selection logic.
- Copy received data into bounded owned storage before returning from SDK callbacks. Define buffer ownership for send requests until completion as well.

Device-specific adapters:

- Are optional compile-time wrappers over a generic transport/session channel, not duplicates of USB/BLE drivers.
- May implement verified vendor endpoint/GATT requirements, extra envelope framing, or connection quirks that do not belong in standard MIDI transport.
- Are selected through the identified device descriptor. Any pre-identification quirk must be keyed by trustworthy observed metadata, not a guessed plugin.
- Device SysEx payload codecs and device command pacing normally remain plugin concerns.
- Do not create empty Pocket Master or GP-5 adapter classes merely to fill a directory; add adapters only for evidenced requirements.
- Place unavoidable pre-connection USB quirks in narrow, declarative identity-keyed configuration consumed by the generic driver.

### 6. State and UI boundary

- Publish a snapshot containing schema version, revision, connection/session status, capabilities, controller selection, device values, certainty, and relevant operation/error status.
- Use bounded lists of blocks and presets with stable IDs, rather than baking Drive/Delay/Reverb and 100 presets into the shared core.
- Version and document maximum counts, message lengths, field sizes, and ownership before implementing the structures.
- Increment revision on observable changes, including timeouts and certainty invalidation, not on every received packet.
- Keep serialization in the web adapter. The UI consumes capabilities and state; it never receives raw MIDI or invokes plugin methods.
- Retain HTTP polling initially; WebSockets are unnecessary for this redesign.
- Introduce `/api/v2/state` and `/api/v2/action` with documented request/result examples. Keep a small v1 compatibility adapter until the embedded UI moves to v2 and the cutover is explicit.
- Distinguish invalid requests, unavailable sessions, unsupported actions, queue saturation, and asynchronous command failures.
- Retain the standalone UI simulator, but drive it with contract-shaped fixtures. Simulation must be explicitly indicated; a failed connection to real firmware must not silently become simulated success.

### 7. Execution, memory, and logging

- One application execution context owns connection state, plugins, operation scheduling, and the read model.
- USB/BLE callbacks enqueue bounded events only; they never mutate plugin/application state or serialize UI snapshots.
- Initially poll the existing web server in the application context; if it later moves to another task, marshal commands and snapshots through the same ownership boundary.
- Inject clock, transport, GPIO source, and logger at the composition root. Keep the core buildable without Arduino or ESP-IDF headers.
- Bound queue counts, payload storage, snapshot size, and log storage. Determine transport capacities from captured maximum valid device messages plus documented headroom.
- Ingress overflow or truncated MIDI invalidates the affected parser state and triggers explicit resynchronization/fault handling. Control-event delivery must remain reliable even if data ingress is saturated.
- Keep input processing ahead of low-priority state refreshes and diagnostic flushing. No blocking waits or sleeps inside core components.
- Structured logs include monotonic timestamp, level, component, event code, session generation, and operation ID where applicable.
- Trace each command through admission, attempted submission, transport completion or failure, and any matching device feedback. Record the stage and reason when the loop remains open; absent feedback is not proof that MIDI delivery failed. Keep tracing bounded and off the time-critical send path.
- Inject a logger interface; bridge to ESP-IDF logging at the platform edge. Compile-time/runtime filtering, rate-limited repetitive diagnostics, dropped-log counters, and opt-in bounded MIDI hex traces support debugging without flooding timing-critical paths.
- Never log Wi-Fi credentials or full identifying data in routine diagnostic captures. Provide deliberate local access to setup credentials rather than mixing them into normal logs.
- Preserve the Python host's existing switch-line feed through a separate explicit compatibility formatter, or deliberately update that integration; do not make it parse the new human log format.

## Suggested source organization

```text
src/
  main.cpp                    composition root only
  core/
    input/                    debounce, gestures, binding validation
    application/              commands, controller state, read model
    connection/               lifecycle and session orchestration
    device/                   shared plugin contracts and capabilities
  devices/
    gp5/                      plugin, codecs, optional evidenced adapter
    pocket_master/            plugin, codecs, optional evidenced adapter
    registry.cpp
  platform/
    esp32/                    GPIO, clock, serial logging
    midi/                     generic MIDI, USB-MIDI, BLE-MIDI adapters
    web/                      HTTP actions, snapshots, asset serving
test/
  native/                     deterministic core/protocol tests
  embedded/                   board and adapter smoke tests
docs/
  architecture.md
  development.md
  adding-a-device.md
CONTRIBUTING.md
AGENTS.md
```

Keep headers beside implementations for new internal components. Keep public contracts easy to find, and avoid moving untouched legacy files solely for cosmetic consistency.

## Developer ergonomics and guidelines

- Retain PlatformIO and the existing ESP-IDF/Arduino integration; do not combine this architecture change with a framework or language migration.
- Pin the validated PlatformIO platform/tool versions and Python bootstrap dependencies; preserve component lockfiles.
- Add a native PlatformIO/Unity test environment reusing the existing test framework. Firmware sources must be filtered out of the native target.
- Add one discoverable check entry point for formatting checks, native tests, embedded compilation, and UI contract checks. Keep hardware flashing and monitoring separate and opt-in.
- Use clang-format and compile-database-backed clangd/clang-tidy for C++; use Ruff for retained Python helpers. Add tools explicitly to the relevant development manifests/configuration.
- Provide VS Code tasks and extension guidance without depending on a contributor's ignored local editor state.
- Add CI for formatting/static checks, native tests, UI contract checks, existing host regressions, and firmware compilation. Hardware tests remain an explicit manual gate.
- Avoid introducing a UI framework or Node build pipeline just for this refactor. Use browser-native contract fixtures and a minimal headless check only where automation needs it.
- Put build/setup/test commands, environment isolation, generated-asset handling, diagnostic capture, and troubleshooting in developer documentation.
- Define allowed dependency directions, task ownership, error/result conventions, certainty semantics, memory limits, and protocol evidence requirements.
- Include an adding-a-device walkthrough covering identity, capabilities, codecs, readback, fixtures, optional adapter criteria, and hardware validation.
- `AGENTS.md` should summarize the same rules and commands, not establish a competing source of truth.

## Implementation todos and dependencies

Prerequisite: land the GP-5 connectivity work and refresh this plan against the resulting baseline. Preserve its verified timings, hub settings, endpoint recovery, protocol handling, and regression tests.

1. **Capture behavioral and protocol baselines.** Record the actual support matrix for USB/BLE and both pedals, preserve existing host tests and switch-feed expectations, extract anonymized protocol fixtures, and document known failures.
2. **Establish the testable foundation and developer tooling.** Add native Unity builds, deterministic fakes, checks, pinned development setup, and initial CI. Depends on 1.
3. **Implement shared contracts and ownership primitives.** Define typed commands/events/results, capabilities, read model, clock/logger ports, bounded ingress, and session generation. Document capacities and API v2. Depends on 2.
4. **Implement switching and gesture recognition.** Add GPIO-independent debounce, configured tap/hold/chords, binding validation, and focused timing tests. Depends on 3.
5. **Implement generic transports and connection sessions.** Adapt known-good low-level drivers behind owned ingress, lifecycle state machines, identification, and completion handling. Add device-specific adapters only when needed. Depends on 3.
6. **Implement and verify both device plugins.** Port only evidenced codecs and operations, capability metadata, synchronization, state certainty, and scheduling. Address relevant response-ID/readback discrepancies using fixtures and hardware captures. Depends on 5.
7. **Compose the application and integrate the UI.** Wire input bindings, local controller navigation, command admission, read-model publication, v2 endpoints, capability-driven rendering, and explicit simulation. Depends on 4 and 6.
8. **Complete developer guidelines and cutover validation.** Finish architecture/contribution/device-extension docs, execute native/CI/embedded/browser checks, perform hardware validation, and remove superseded paths only after acceptance. Depends on 7.

Steps 4 and 5 can proceed independently after the contracts are established. Keep replacement development isolated in a branch and explicit alternate firmware target until cutover; do not build a permanent parallel implementation or erase the dirty working tree.

## Acceptance criteria

- Core input/application/plugin code builds and tests natively without ESP32 SDK headers.
- Input tests cover bounce, immediate press, tap/hold exclusivity, chord window boundaries, overlapping chord priority, incomplete chords, release/rearm, startup-held switches, and timer wraparound.
- Ordinary immediate bindings incur no gesture-window delay after debounce; deferred bindings obey the configured timing exactly under the fake clock.
- Session tests cover attach, unsupported identity, ambiguous identity, sync failure, disconnect during operation, retry exhaustion, same/different pedal reconnect, and stale callbacks.
- A connected, identified session continues to submit independent live commands during sync timeout or missing feedback; diagnostic and UI work cannot starve that output. A command that requires readback remains subject only to its own readback policy.
- Saturation tests show bounded memory, visible queue errors, preserved disconnect handling, and safe parser reset after receive loss.
- Transport tests cover fragmented/coalesced MIDI, multiple BLE-MIDI timestamps/messages, realtime interleaving, malformed/truncated frames, SysEx limits, and send submission versus completion failures.
- Plugin fixtures verify exact output bytes and readback interpretation for both devices. Unknown IDs and malformed responses cannot confirm state.
- Unknown toggle behavior follows the agreed readback-first policy, with explicit timeout failure where readback is supported.
- State tests verify confirmed/assumed/unknown transitions, actual-change revisions, capability handling, and no stale session state.
- Browser tests verify both device capability fixtures, explicit errors, disconnect state, and visible simulation mode.
- Existing Python host tests remain green; the serial switch-feed integration is preserved or intentionally updated and tested.
- Firmware compiles with preserved board pins, USB hub configuration, and embedded assets. Native tests do not replace embedded compilation.
- Physical validation checks both pedals over verified supported transports, reconnects, preset/effect actions, synchronization, and simultaneous Wi-Fi activity. Unverified BLE identity or unresolved GP-5 sync remains explicitly unsupported, not marked complete.
- Capture measured input responsiveness, queue high-water marks, heap/stack headroom, and error counters during sustained traffic with logging enabled and disabled.
- Default live-control command paths contain no persistent preset/editor writes.
- A new device can be added through its descriptor/plugin/codecs/tests and an optional justified adapter without modifying switching, shared application routing, or adding vendor branches to generic transports.

## Important trade-offs

- This is a replacement design with staged validation, not a cosmetic reorganization of the current main loop.
- Reliable auto-identification takes precedence over claiming BLE support. Its feasibility is an explicit early investigation gate.
- Capability-driven typed state is slightly more work than a fixed four-block struct, but avoids repeating this redesign for every new device.
- Exclusive gestures necessarily defer some actions; immediate controls remain immediate by explicit binding policy.
- A single application state owner and bounded queues are intentional simplifications for this embedded controller.
- Protocol and hardware reliability must be demonstrated separately from architectural cleanliness.
