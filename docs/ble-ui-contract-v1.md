# StompBridge live UI contract v1

Status: v1 contract implemented as an opt-in experimental firmware target and
Web Bluetooth adapter. Native protocol tests and target compilation pass. The
firmware has been flashed and BlueZ discovered the UI service. Browser
interoperability, pairing, notifications, live commands, and authorization
timing remain open. See the [manual check record](ble-ui-manual-checks.md).

## Semantic JSON messages

HTTP v2 and the BLE UI adapter use the same JSON objects and semantic names.
BLE frames carry UTF-8 JSON; BLE-specific framing does not enter `src/core`.
Unknown fields are ignored. Required fields missing, duplicate JSON keys,
invalid UTF-8, trailing JSON values, wrong types, or an unsupported major
version are rejected. Integers are unsigned decimal values within the widths
shown. The decoder must reject duplicate keys rather than choosing first/last.

`hello` negotiates the major and minor protocol and binds the request to this
connection's UI epoch. `generation` is the pedal session generation observed
by the UI; clients must refresh state after reconnect and must not replay old
commands.

```json
{"v":1,"kind":"hello","min":1,"max":1,"epoch":3}
```

The response reports selected version, epoch, the current pedal generation,
and supported feature bits. A range with no common major is rejected with
`versionMismatch`; no command is accepted before successful negotiation and
state subscription. Clients subscribe before sending `hello` so the hello result
can be delivered as a notification.

Commands are absolute semantic intent. `setPreset` targets an absolute preset
index; `setBlock` targets a stable block ID and explicit boolean. `setTuner`
is also absolute. No browser-side toggle is sent, and no bank-relative preset
or raw MIDI command is accepted. `snapshot` requests a full fresh snapshot.

```json
{"v":1,"kind":"command","request":41,"epoch":3,"generation":12,
 "command":{"kind":"setPreset","index":6}}
{"v":1,"kind":"command","request":42,"epoch":3,"generation":12,
 "command":{"kind":"setBlock","id":2,"enabled":false}}
{"v":1,"kind":"command","request":44,"epoch":3,"generation":12,
 "command":{"kind":"setTuner","enabled":true}}
{"v":1,"kind":"snapshot","request":43,"epoch":3,"generation":12}
```

The admission result is separate from execution. `accepted` means the
application admitted the semantic command. Queue admission, a successful GATT
write response, and `Submitted`/`TransportComplete` do not mean the pedal
executed or observed it. A result is correlated by request ID and, after
application admission, operation ID:

```json
{"v":1,"kind":"result","request":41,"epoch":3,"generation":12,
 "admission":"accepted","operation":19,"phase":"queued","result":"accepted"}
{"v":1,"kind":"result","request":41,"epoch":3,"generation":12,
 "admission":"accepted","operation":19,"phase":"observed","result":"accepted"}
```

Admission values map to core `Result`: `accepted`, `unsupported`, `notReady`,
`queueFull`, `invalid`, and `interrupted`. Execution phases map to core
`OperationPhase`: `queued`, `submitted`, `transportComplete`, `observed`, and
`failed`. Failure results preserve the specific core result. `observed` is
published only when the plugin reports verified feedback. `Assumed` is never
rendered as `Confirmed`.

A snapshot is an immutable serialization of one `ReadSnapshot` revision. It
contains protocol schema, revision, pedal generation, detected device ID (or
`unknown`), local bank, connection phase, sync phase/result, capabilities, current absolute preset and certainty,
the four names/labels in the visible bank, tuner state/certainty, and supported
block values, effect model names, and their certainties. It does not contain
the full preset directory.
Unsupported names are empty/absent, never fabricated as device feedback.

```json
{"v":1,"kind":"snapshot","epoch":3,"generation":12,"revision":88,
 "bridge":"connected","device":"valeton_gp5",
 "pedal":{"phase":"ready","sync":"confirmed",
 "syncResult":"accepted"},"bank":1,
 "capabilities":{"presetCount":100,"presetReadback":true,"tuner":true,
 "blocks":[{"id":2,"label":"Drive","set":true,"readback":true}]},
 "preset":{"index":6,"certainty":"confirmed","name":"Glass Horizon"},
 "visiblePresets":[{"index":4,"label":"P04","name":"Clean"},
 {"index":5,"label":"P05","name":"Edge"},
 {"index":6,"label":"P06","name":"Glass Horizon"},
 {"index":7,"label":"P07","name":"Wide"}],
 "tuner":{"enabled":false,"certainty":"unknown"},
 "blocks":[{"id":2,"enabled":true,"certainty":"confirmed",
 "effect_name":"Overdrive","model_certainty":"confirmed"}]}
```

Snapshot updates are coalesced by revision. If a transfer is in progress, its
bytes remain immutable and complete before the newest revision begins. Results
and command replies have priority over state refreshes. A bounded history of
eight correlated result records is retained. If the live result queue overflows,
the server sends a `resultGap` error with the loss count and oldest retained
sequence; the browser marks state stale and requests a fresh snapshot. A command
with no correlated result after its local deadline is shown as unknown after 20 seconds and is never replayed. The browser requests
snapshots every 8 seconds and marks live state stale after 12 seconds without a
fresh snapshot. The gap notification is:

```json
{"v":1,"kind":"error","code":"resultGap","lost":2,"oldest":8}
```

Notifications are not receipts. A malformed, duplicate,
reordered, or timed-out transfer is discarded. The browser throttles recovery
requests, marks pedal state stale, and asks for a fresh snapshot.

## GATT and frame envelope

Custom 128-bit service UUID: `9e0d0001-7f4a-4b86-9c31-6a8b7d4c2000`.

| Characteristic UUID suffix | Properties | Purpose |
| --- | --- | --- |
| `...2001` | Read | Protocol major/minor, feature bits, max command/snapshot sizes, access-window state. |
| `...2002` | Write with response | Negotiation, snapshot request, and command fragments. |
| `...2003` | Notify | Negotiation response, result, snapshot, and error fragments. |

Each characteristic value is one frame. Multibyte header fields are little
endian. The exact 12-byte header is:

| Byte(s) | Meaning |
| --- | --- |
| 0–1 | Magic `53 42` (`SB`) |
| 2 | Protocol major, currently `01` |
| 3 | Message type: 1 hello, 2 command, 3 snapshot, 4 result, 5 resync |
| 4–5 | Transfer ID, wrapping `uint16`, scoped to connection epoch |
| 6–7 | Zero-based fragment index |
| 8–9 | Fragment count, 1–1,024 |
| 10–11 | Total UTF-8 JSON message length in bytes |
| 12… | UTF-8 JSON bytes |

Fragments are consecutive and ordered. All non-final fragments in one transfer
use the sender's first-fragment data length; the receiver learns this chunk size
from fragment zero, allowing a client to keep writing 20-byte frames after MTU
negotiation. Duplicate, reordered, missing, inconsistent, and
malformed fragments are explicit errors; duplicates are not silently applied.
The reassembly lifetime is 3,000 ms from the last accepted fragment. Timeout
discards the partial transfer and requires fresh snapshot recovery. Reassembly
uses fixed storage and accepts at most 8,192 message bytes. Fragment indexes
and counts are unsigned 16-bit values.

The maximum snapshot bound is 8,192 bytes. It is derived conservatively from
the fixed root envelope and 32-bit values (768 bytes), 16 capability records
with 23-byte strings escaped at six bytes per input byte (3,264 bytes), 16
block-state records (1,600 bytes), four visible preset records with maximally
escaped 24-byte labels and 16-byte names (1,280 bytes), the current preset
record (256 bytes), plus 1,024 bytes of serializer/key variation margin. The
JSON writer enforces this bound and a native fixture exercises maximum
record counts plus escaped control/quote bytes. The fixture does not exhaustively
cover every possible UTF-8 string combination, so the arithmetic remains a
conservative schema bound rather than a proof of a globally maximal encoding.

ATT MTU 23 gives 20 bytes of characteristic payload, leaving 8 data bytes per
frame. An 8,192-byte message therefore takes at most 1,024 frames. At ATT MTU
517, the 514-byte characteristic payload leaves 502 bytes and takes at most 17
frames. The server must use negotiated MTU minus the three ATT header bytes
and still work at the default MTU. Maximum UTF-8 JSON sizes are 256 bytes for
hello, command, or result, and 8,192 bytes for a snapshot. An attempted larger
message fails before any fragment is emitted.

## Queue and connection bounds

- One subscribed UI client and one active pedal are accepted.
- The callback event queue holds 12 bounded events; overflow disconnects the
  UI client and requires fresh physical authorization.
- Four UI commands may wait in the application ingress queue. The application
  queue holds 16 commands plus one active operation; the BLE correlation table
  has 17 slots and refuses admission if no slot is available.
- Eight results may wait for notification and eight correlated results are
  retained. Queue loss is reported with `resultGap`.
- Request IDs are nonzero and strictly increasing for all UI requests within a
  UI epoch; duplicate or older IDs are rejected. This gives bounded duplicate
  detection.
- Eight correlated results are retained; the zero-based result cursor reports
  the oldest retained entry and advances on overwrite. A client that detects a
  gap requests a fresh snapshot/reconciliation.
- One in-flight snapshot plus one replaceable newest-revision marker; no queue
  of historical snapshots.
- Command IDs are unique within one UI epoch. A duplicate ID is rejected and
  never re-executed. Transfer IDs are transport identifiers, not command IDs.
- Disconnect cancels queued requests for that UI epoch. Commands already
  handed to the application are not assumed canceled; their final state is
  reported only if the epoch is still connected. No reconnect replay.
- Each accepted command carries the current pedal generation. A stale UI epoch
  or pedal generation is rejected before `Application::submit`. Pedal loss
  invalidates observed state and is published to the still-connected UI.

## Writable UI access policy

The screen requires a deliberate physical authorization action on the bridge.
On the experimental target this is a long hold of the utility switch (the
tenth switch, firmware index 9). It opens a 60-second window for one UI peer to
connect, encrypt, and bond. Once established, the session continues after the
window expires. A disconnect requires a new physical authorization before
reconnecting. A utility-switch hold while connected revokes the active UI link
and invalidates its epoch. Authorization is bridge-side policy, separate from pedal BLE security.
Service UUID, advertised name, browser chooser approval, and protocol
negotiation are not authorization.

The first proof environment must be controlled and physically supervised.
Bluefy on iPhone established an encrypted, bonded session and exercised
control with a GP-5. Physical revoke, reconnect, and Chrome on macOS remain
open. Linux browser discovery also remains unresolved in the recorded trials.
See the [manual check record](ble-ui-manual-checks.md) for client evidence.

## Native codec verification

The implemented vertical slice includes fixed-storage frame/reassembly code,
a four-entry epoch/generation-scoped BLE ingress, an eight-entry correlated
result ledger, the GATT service, application-task command handoff, and browser
transport/UI. Native boundary behavior is tested with:

```sh
./scripts/pio test -e native -f test_native_ble_ui
```

The ten test cases cover default-MTU fragmentation/reassembly, a larger MTU,
maximum and over-limit messages, empty messages, malformed headers,
unsupported versions, duplicate and reordered fragments, timeout, bounded queue
saturation, duplicate request IDs, stale UI epochs/pedal generations,
disconnect cancellation, correlated failure lookup, result-history overflow,
and unchanged read-model certainty. JSON boundary behavior has native fixtures;
GATT and browser interoperability, security behavior, and device behavior still
require the manual hardware/client checks.
