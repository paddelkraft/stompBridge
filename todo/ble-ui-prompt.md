# Prompt: implement the Bluetooth screen UI

Use this prompt to start the first implementation stage of the
[Bluetooth UI plan](./ble-ui-plan.md).

```text
Implement the first concrete stage of StompBridge's Bluetooth screen UI.
Read todo/ble-ui-plan.md as the agreed scope and architecture, then read
ui/README.md, docs/architecture.md, docs/development.md,
docs/support-matrix.md, and todo/architecture-replacement-plan.md.
Inspect the current code, git status, build targets, and existing tests before
editing. Documentation and generated SDK configuration are evidence to verify,
not proof that a feature is implemented or hardware-validated. Preserve
existing uncommitted work. Do not commit unless I ask.

Goal: reuse the existing HTML UI for live control/status over a custom
StompBridge BLE GATT service while the screen keeps its current Wi-Fi
connection. Start with one screen and one active USB pedal on the current
ESP32-S3, using Chrome on Mac and Bluefy on iPhone/iPad. Qualify Linux
separately. Do not claim ordinary Safari or Firefox support. Load the static
UI over HTTPS; BLE carries commands and state, not the web page.

Implement, do not merely produce another plan. Start by specifying the bounded
versioned live UI contract: commands, snapshots, request/admission/execution
results, protocol negotiation, frame layout, maximum encoded sizes, queue
limits, fragment timeouts, and connection access policy. Reuse the replacement
core's semantic commands, capabilities, certainty, operation phases, and
session generations. Keep the core independent of BLE, JSON, and browser APIs.
Document concrete wire examples and add focused tests before wiring hardware.

Work toward a separate opt-in experimental firmware target with a vertical
slice: browser connects, negotiates protocol, subscribes, obtains a coherent
snapshot, selects an absolute preset or controls an effect, and displays the
application result and subsequent pedal state. Use the existing verified USB
pedal path first. Preserve production and existing diagnostic targets and
legacy HTTP v1. Share the planned HTTP v2 semantic boundary with BLE rather
than creating a second controller or device state model.

Initialize NimBLE once through a small shared platform owner. Keep the
pedal central/client transport separate from the UI peripheral/server.
Callbacks copy bounded data and enqueue events; only the application task
mutates Application, plugins, and the read model. Publish immutable snapshots
and correlated results across task boundaries. Keep UI connection epochs
distinct from pedal generations. UI disconnect must not disturb pedal control
or physical switches; pedal disconnect must remain visible to the UI.

Use a custom service UUID, protocol information, command writes with response,
and state/result notifications. Support the default ATT MTU of 23, with only
20 bytes of attribute payload, as well as larger payloads. Serialize browser
GATT operations. Bound all messages, buffers, queues, and reassembly lifetime.
Detect malformed/incomplete transfers explicitly and provide a fresh-snapshot
recovery path. Coalesce state updates and keep snapshots coherent; send only
the current preset and visible bank names initially, not the full directory.
Prioritize live commands and correlated results over background UI traffic.

A GATT write response is not command admission or pedal execution. Accepted
is not sent; transport completion is not observed pedal state. Preserve
Unknown, Assumed, Confirmed, and degraded synchronization behavior. Do not
rely on a single last-operation field to report all outstanding commands.
Cancel queued UI work on UI loss and stale pedal generations. Do not
automatically replay commands on reconnect or retry uncertain toggles.

Extract only the small browser transport boundary needed for BLE, HTTP, and
explicit simulation. Reuse rendering without adding a frontend framework or
unnecessary build pipeline. Show bridge-link and pedal-link status separately,
plus stale state, pending operations, errors, and certainty. A failed real
connection must never silently become simulated success.

Include a deliberate physical authorization/connect window for writable UI
access. Do not mistake browser permission, service UUIDs, or device names for
device-side authorization. Verify client encryption/bonding compatibility and
define revocation/reconnect behavior before recommending use outside the proof
environment. Keep this policy separate from pedal BLE security requirements.

Use existing test/build tooling. Cover contract encoding/decoding, 20-byte
fragmentation, size boundaries, malformed/missing/duplicate/reordered fragments,
timeout, version mismatch, queue saturation, stale epochs/generations, correlated
failures, and unchanged certainty. Run the smallest relevant tests and builds;
escalate to the repository check entry point as appropriate. Update directly
related documentation with exact commands and observed results.

When hardware and clients are available and I authorize device testing, test
Mac Chrome and Bluefy against a USB pedal: snapshots, live commands, physical
switch updates, either-link reconnect, and screen lock/background/foreground.
Measure latency and memory rather than assuming them. If hardware, HTTPS
hosting, or an iOS client is unavailable, complete locally testable work,
provide reproducible manual steps, and mark those checks unverified. Do not
publish externally, flash a board, or change system Bluetooth/network settings
without approval.

Stop the initial stage at a reviewable USB-pedal plus BLE-screen vertical slice.
Do not claim simultaneous BLE-pedal support from it. That later stage must
implement and verify the replacement core's BLE pedal identity, complete
BLE-MIDI framing, and session integration before testing both BLE roles
together. BLE-MIDI service advertising alone must not select a pedal plugin.
Native wrappers, offline delivery, shared-network Wi-Fi, configuration editing,
multiple screens/pedals, and firmware updates remain out of scope.

Finish with changed behavior, tests/build evidence, actual hardware checks,
remaining unverified gates, and the next smallest implementation stage.
```
