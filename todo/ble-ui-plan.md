# Bluetooth screen UI: architecture exploration and proposed implementation

## Goal and agreed boundaries

Reuse the existing HTML UI as a live-control screen over BLE without making
the screen join StompBridge's Wi-Fi access point. Target the current ESP32-S3.

Agreed first-version scope:
- One connected screen and one active pedal, over USB or eventually BLE.
- Live preset selection, bank navigation, effect/tuner control, and status.
- Chrome on Mac and Bluefy on iPhone/iPad; qualify Linux explicitly.
- Loopback HTTP for desktop iteration and trusted HTTPS for phone testing.
  Internet-free delivery to other devices is deferred.
- Test Bluefy before considering a native wrapper.
- No configuration editor, firmware update, audio streaming, multiple-pedal
  routing, or multi-screen arbitration.

## Implementation status

- The opt-in `stompbridge-next-ble-ui` target combines the replacement USB
  pedal core with a NimBLE GATT server. The [live UI v1 contract](../docs/ble-ui-contract-v1.md)
  covers framing, bounded commands, correlated results, snapshots, and access.
  Live UI v1 is distinct from the legacy HTTP v1 API.
- The [browser UI](../ui/README.md) has explicit BLE, USB DEV, simulator, and
  legacy HTTP paths. BLE discovers the custom service, reads protocol info,
  subscribes, negotiates v1, and requests a snapshot. The bridge requires
  encrypted GATT access and a utility-switch hold for the writable window.
- USB DEV has delivered GP-5 snapshots in a real browser. The 10 native BLE
  framing and ingress tests passed on 2026-11-19. The BLE target was flashed
  and host BlueZ verified the advertised service and **StompBridge UI** name.
  The GP-5 reported patch and effect names over USB. Browser discovery,
  pairing, notifications, commands, and reconnect remain open. Brave/Linux
  omitted the bridge even in an unfiltered chooser while BlueZ rediscovered
  it. LightBlue on iPhone discovered the same bridge. A protected GATT read
  in LightBlue is the next phone check. Mac Chrome on loopback remains the
  desktop comparison. The latest build resumes advertising after rejected
  connections. A BlueZ connect attempt with authorization closed verified
  that the bridge disconnects and is discoverable again. LightBlue now sees
  it as connectable. The UI target now requests the bond its access checks
  require, and the LightBlue retry logged an encrypted and bonded connection.
  LightBlue read the protocol-info characteristic and returned the v1
  prefix. Brave still omitted the bridge after both firmware fixes, including
  in an unfiltered retest. Use another browser platform to verify negotiation
  and live control. Disconnect LightBlue before the browser attempt.
  See the [manual check record](../docs/ble-ui-manual-checks.md).
- Shared HTTP v2 and replacement-core BLE pedal integration remain future work.
  Neither is a dependency for the desktop BLE screen with a USB pedal.

## Feasibility and qualifications

The intended topology is feasible in principle:

```text
Mac Chrome / iPhone or iPad Bluefy / qualified Linux browser
                  BLE central + GATT client
                              |
                  custom StompBridge UI service
                              |
ESP32-S3: BLE peripheral + GATT server
          shared BLE runtime
          BLE central + GATT client -------- BLE-MIDI pedal
          USB host ------------------------ USB-MIDI pedal (alternative)
```

The screen connects to StompBridge, not directly to the pedal. The two BLE
links have opposite GAP roles and share one controller/radio. Simultaneous
reliability and latency remain hardware validation gates. This requires a
compatible BLE pedal service; ESP32-S3 does not support Bluetooth Classic.

The local generated `sdkconfig.esp32-s3-n16r8` inspected during investigation
enables central, peripheral, observer, and broadcaster roles, three NimBLE
connections, and six controller activities. This is encouraging configuration
evidence, not proof of working dual-role behavior. Recheck it and make required
settings reproducible in tracked configuration for the experimental target.

BLE leaves the screen's existing Wi-Fi connection intact. The browser loads
the static page separately and uses GATT for commands and status.
Chromium treats loopback HTTP as a secure context. Use
`./scripts/serve-ui-local` and open `http://localhost:8000/` on the same
computer. Ordinary HTTP on a LAN IP or at `http://192.168.4.1/` does not get
the loopback exception. [Secure Contexts specification](https://w3c.github.io/webappsec-secure-contexts/#is-origin-trustworthy)

### Browser constraints

| Screen | First route | Qualification |
| --- | --- | --- |
| Mac | Chrome | Web Bluetooth GATT support; user-initiated chooser and OS Bluetooth permission required. |
| iPhone/iPad | Bluefy | App installation required; service discovery, notifications, reconnect, and foreground behavior need testing. |
| Linux | Chromium/Chrome compatibility trial | Upstream describes partial/unsupported Linux support requiring experimental features; qualify a concrete distro, browser, adapter, and BlueZ combination. |
| Safari / Firefox | Not the BLE browser route | Do not assume native Web Bluetooth support. Installing a normal Safari PWA does not add it. |

If Bluefy proves unsuitable, the same GATT protocol could support a native
CoreBluetooth wrapper around the UI. HTTP on a shared Wi-Fi network could
preserve ordinary Safari compatibility, but station-mode networking is a
separate feature, outside this proof of concept.

## Existing architecture and integration

Relevant files at investigation time:
- [UI README](../ui/README.md) and [browser app](../ui/app.js): simulator and
  legacy HTTP v1 client.
- [HTTP server](../src/web_ui.cpp): access-point-only asset/state/action server.
- [Legacy composition](../src/main.cpp): action queue and v1 serializer,
  initially selecting USB.
- [BLE-MIDI transport](../src/ble_midi_transport.cpp): NimBLE initialization,
  central discovery, notifications, and writes.
- [Application](../src/core/application.h): semantic `submit`, local `bank`,
  and access to `snapshot`.
- [Contracts](../src/core/contracts.h) and [read model](../src/core/read_model.h):
  results, generations, capabilities, revisions, certainty, and operation phases.
- [Architecture](../docs/architecture.md) and
  [replacement plan](./architecture-replacement-plan.md): ownership and planned
  HTTP v2 boundary.
- [Experimental USB composition](../src/next_usb_main.cpp): core-backed USB
  integration, without UI at investigation time.

Build on the replacement core, not a second controller around legacy v1.
Preserve production and diagnostic environments; use a separate opt-in
experimental composition target.

```text
UI views
  |
explicit simulator / HTTP adapter / BLE adapter
  |
one versioned semantic UI contract
  |
bounded command ingress -> application task -> Application
                                             |         |
                                    device plugins   ReadModel
                                             |         |
                                        USB/BLE     snapshot publisher
                                                       |
                                            HTTP responses / BLE events
```

Generalize "serialization in the web adapter" to "serialization at the UI
boundary." Reuse the v2 semantic schema and serialization between HTTP and BLE
where practical; BLE framing remains transport-specific. No BLE SDK, JSON, or
browser dependencies enter the native core. HTTP v2 is planned, not an
implemented API at investigation time.

## Design decisions

### Shared BLE runtime, independent connections

Move global NimBLE initialization/lifecycle out of the pedal-only transport
when introducing the UI service. A small platform owner initializes NimBLE
once and coordinates advertising/scanning and shared resources.

Keep the pedal central/client transport separate from the UI GATT server.
The UI service is not a `MidiTransport`, does not reuse BLE-MIDI UUIDs as a UI
API, and does not expose raw pedal MIDI to the browser.

Separate UI connection epochs from pedal session generations. UI loss must not
disconnect the pedal, reset the application, or stop physical switches.
Pedal loss invalidates pedal state and queued session-bound commands but
leaves the UI connected to display the fault.

### Minimal custom GATT service

Start with readable protocol/version/capability information, a command
characteristic accepting bounded writes with response, and an event
characteristic for admission/results and state notifications.

Use a versioned frame envelope with message type, message/transfer identifier,
length, and fragment ordering. Inspect the effective ATT payload and support
the default MTU of 23 (20-byte attribute payload), not just an assumed large
phone MTU. Serialize browser GATT operations.

Before implementation, derive and document maximum command, event, and snapshot
sizes from the bounded core schema and worst-case encoded strings. Bound
buffers, queues, and incomplete-transfer lifetime. Reject malformed/oversized
data explicitly; no unbounded logs or preset streaming.

Initially publish coherent full live snapshots on observable revision changes,
coalescing redundant refreshes. Include the current preset and visible bank
names; defer the full preset directory. Keep an in-flight snapshot immutable
and finish it before starting a newer transfer.

Subscribe first, request a snapshot, then receive updates. Missing/invalid
transfers require explicit snapshot resynchronization; notification delivery
is not an application receipt. Prioritize command results over periodic state
traffic without starving pedal processing.

### Shared commands, state, and ownership

- Both HTTP and BLE submit semantic actions through the application owner;
  browser clicks do not impersonate GPIO edges.
- Use absolute preset/block targets so a changed bank cannot redirect a command.
- Return request correlation, typed admission, operation ID where applicable,
  and later execution status. A GATT write response only acknowledges delivery
  to the attribute, not application acceptance or pedal execution.
- Publish bounded correlated results separately; the read model's single
  last-operation field cannot report all outstanding commands. Report lost
  results/unknown outcomes explicitly.
- Accepted is not sent; TransportComplete is not pedal-observed. Only verified
  feedback establishes Confirmed. Preserve unknown/degraded values and tuner
  limitations.
- Callbacks copy bounded input and enqueue it. They do not call `Application`,
  plugins, or snapshot serialization from the NimBLE task. The application
  context owns mutation and publishes immutable snapshots.
- Scope queued commands to the UI epoch and intended pedal generation. Cancel
  queued UI commands on UI loss and never replay on reconnect.
- Use a bounded duplicate-request policy within the connection. Do not retry
  toggles or operations with an uncertain outcome automatically. Fetch fresh
  state before enabling live controls after reconnect.

### UI behavior and access

Extract a small transport interface from the [browser app](../ui/app.js) for
connect, disconnect, send, snapshot, and error events. Reuse rendering; keep
simulation explicitly selected and labeled. Failed real connections must not
silently become simulated success.

Show screen-to-bridge status separately from bridge-to-pedal status, plus
pending/failed operations, stale state, and certainty. Do not display
optimistic values as confirmed.

Connect through a user gesture and custom service filter. Serve static assets
from loopback HTTP on desktop or trusted HTTPS on the phone. Negotiate protocol
compatibility and reject incompatible firmware clearly. Preserve existing
access-point/HTTP behavior.

Include a deliberate physical connect/authorization window for writable UI
access. UUIDs, names, and the browser chooser are not device-side authorization.
Validate BLE encryption/bonding on target clients and define revoke/reconnect
behavior before use outside the proof environment. Keep UI access policy
separate from pedal BLE security requirements.

## Next steps

1. **Desktop browser and BLE adapter.** Load the local UI and check
   `window.isSecureContext` and `Boolean(navigator.bluetooth)` in DevTools.
   Start on the development desktop. On Linux, check that BlueZ sees a powered
   adapter and try Chromium's Experimental Web Platform features flag if the
   API is missing. Record the browser, OS, and adapter. Use Mac Chrome for the
   baseline if the Linux browser cannot complete the trial.
2. **Desktop BLE with the USB GP-5.** With the flashed
   `stompbridge-next-ble-ui` target and GP-5 on USB, hold the utility switch,
   then click **CONNECT BLE** within 60 seconds. Check
   chooser discovery, encrypted protocol read, notification subscription, and
   the first snapshot. Record which stage fails if the connection stalls.
3. **Live control and reconnect.** Change a preset and effect in the UI and
   verify pedal readback. Change the pedal physically and check the UI.
   Disconnect each link separately, then verify fresh state and no replay.
   Exercise authorization expiry and revocation. Record findings in the
   [manual check record](../docs/ble-ui-manual-checks.md).
4. **Phone verification.** Open the [hosted StompBridge UI](https://paddelkraft.se/stompbridge/)
   in Bluefy on iPhone/iPad. The HTTPS deployment serves the current static UI
   and requires no certificate profile on the managed phone. Disconnect
   LightBlue before connecting, then verify the first GP-5 snapshot, live
   controls, screen lock, foreground recovery, and reconnect. Bluefy reported
   the initial session dropping at about 60 seconds. The firmware now keeps
   encrypted, bonded sessions active after the connection window expires.
   The bridge log confirmed the session survived the original deadline.
   The iPhone then auto-locked. The browser UI now requests a screen wake lock
   for the visible BLE session. Redeploy the static UI and verify Bluefy shows
   **Screen wake lock active** and can change presets after the auto-lock interval.
   Check physical revocation separately. Bluefy's bottom toolbar remains
   visible. The compact UI fits both grids, the current preset, and bank
   navigation in short emulated portrait and landscape viewports. Verify
   safe-area spacing and touch targets in Bluefy after redeploying the CSS.
5. **Later firmware and release work.** Integrate a verified BLE pedal in
   the replacement core, qualify simultaneous BLE roles, and capture the
   release measurements below. Adapt HTTP v2 to the shared semantics when
   that work resumes.

## Verification and release gates

- Native tests: admission, stale generations/epochs, queue saturation,
  duplicates, correlated failures, and unchanged certainty policies.
- Protocol tests: 20-byte and larger payloads, maximum encoded sizes, missing/
  duplicate/reordered/oversized/malformed fragments, timeout, version mismatch.
- UI contract tests: equivalent HTTP/BLE interpretation, distinct link states,
  pending/failure/stale behavior, and no demo fallback.
- Build the opt-in and affected existing firmware targets with existing tooling.
  Flashing and shared-system setup changes remain explicit hardware-test steps.
- Hardware matrix: Mac Chrome and Bluefy, first with USB and then with each
  BLE pedal whose identity/protocol is verified.
- Exercise presets, toggles, banks, tuner assumptions, physical switches,
  either-link reconnect, scanning/advertising overlap, screen lock/background/
  foreground, and busy existing Wi-Fi on the screen.
- Measure input-to-send and input-to-observed latency separately, UI refresh
  latency, dropped/overflow counters, heap/stack high-water usage, and sustained
  stability. Agree numeric acceptance limits before performance sign-off.
- Verify UI loss does not disturb pedal/footswitch operation, pedal loss stays
  visible, and neither reconnect replays stale commands.
- Foreground control is the target; do not promise persistent iOS background BLE.

## Evidence sources

- [MDN Web Bluetooth](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API):
  GATT scope, secure context, permission and gesture requirements.
- [Implementation status](https://github.com/WebBluetoothCG/web-bluetooth/blob/main/implementation-status.md):
  Mac support, Linux limitations, iOS restrictions and alternative clients.
- [Browser compatibility](https://caniuse.com/web-bluetooth):
  native Safari/iOS and Firefox support differs from extension/polyfill support.
- [Bluefy listing](https://apps.apple.com/us/app/bluefy-web-ble-browser/id1492822055):
  vendor-described iOS/iPadOS GATT browser and HTTPS requirement, not proof of
  compatibility with this application's eventual protocol.
- [ESP32-S3 BLE overview](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/ble/overview.html):
  BLE/NimBLE availability and absence of Classic Bluetooth. Current stable docs
  are capability context, not a substitute for the repository's pinned stack.
