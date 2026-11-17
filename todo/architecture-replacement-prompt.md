# Resume the StompBridge architecture replacement

Use this prompt after the GP-5 connectivity work has landed:

```text
Implement the deferred StompBridge architecture replacement described in
todo/architecture-replacement-plan.md.

First read the plan, current repository guidance, README, device documentation,
tests, and the latest GP-5 connectivity changes. The plan's review observations
are historical, not assertions about today's code.

Before changing firmware:
1. Confirm the GP-5 connectivity prerequisite has landed. If its status is
   unclear, ask me rather than assuming or starting a connectivity rewrite.
2. Inspect the worktree and preserve all unrelated changes.
3. Refresh the support matrix and identify which plan assumptions are now stale.
4. Capture the working GP-5 behavior in focused regression fixtures/tests,
   including relevant USB hub settings, timings, endpoint recovery, protocol
   handling, and synchronization behavior. Preserve evidence-backed fixes.
5. Present material deviations or unresolved behavioral choices for agreement.
   Do not expand scope or redesign working low-level drivers unnecessarily.

Then implement in the plan's dependency order, using small verifiable stages:
- Native test/tooling foundation.
- Typed contracts, bounded ownership, logging, and state model.
- Independent switching/gesture subsystem.
- Connection manager, sessions, generic transports, and only justified
  device-specific adapters.
- Device plugins and verified protocol/state handling.
- Application composition and capability-driven web UI.
- Developer guidelines, CI, and cutover validation.

Preserve the agreed constraints: one active pedal, compile-time plugins,
automatic verified identification only, no disconnected command replay,
readback-first unknown toggles, explicit state certainty, no persistent editor
writes, and Python host retained as a diagnostic/reference tool.

Keep known-good firmware available until replacement acceptance. Run targeted
tests at each stage and embedded compilation where relevant. Do not flash
hardware without my explicit request. Clearly separate automated validation
from physical checks still needed; never claim BLE support, GP-5 reliability,
or hardware validation without evidence.

Update the plan as stages are completed and report changes, validation results,
remaining limitations, and the next step. Do not commit unless I ask.
```
