# Development checks

Use Python 3.13 and run `./scripts/setup-dev` once in a fresh checkout. The
setup script refuses to replace an existing `.venv`. The project keeps
PlatformIO packages under `.platformio`; `dependencies.lock` pins ESP-IDF
components. The current checkout had a stale `.venv` launcher at baseline,
so initial validation used a disposable PlatformIO 6.2.0 environment. It was
then repaired with `python3 -m venv --upgrade .venv` and
`.venv/bin/python -m pip install -r requirements-dev.txt`.

Run `./scripts/check` for native GP-5 protocol fixtures, USB configuration
guards, Python host regressions, native Unity tests, and embedded compiles.
It does not upload or monitor hardware. Run individual stages with
`./scripts/check-gp5-baseline`, `./scripts/pio test -e native`, and
`./scripts/pio run -e esp32-s3-n16r8`.
The isolated `stompbridge-next-input` target compiles the new core and runs
only an input diagnostic if deliberately uploaded. The check script compiles
it; it does not upload any target.
The separate `stompbridge-next-usb` target compiles an experimental USB
application session. It automatically sends read-only sync requests after
USB descriptor identification and supports physical switch commands if
deliberately uploaded. The check script compiles it without uploading.
Its serial console reports `SYNC gen=N pending`, `confirmed preset=N` with
block values, `failed reason=...`, or `interrupted by live command`. These
lines report device readback; successful MIDI transmission alone does not
produce `confirmed`.
After GP-5 sync, a background request can report current effect model IDs and
`PATCH NAMES gen=N count=100`, followed by `PATCH P00` through `PATCH P99`.
The name list is printed only after every fragment and record index validates.
For sync diagnosis, this target also prints up to 20 bounded `SYNC RX` frame
summaries per USB attachment (`len`, first 16 MIDI bytes, last four bytes, and
the block bitmap window when present). Copy those lines together with the
`SYNC` result and the preceding USB identity line. Routine per-transfer USB
traffic is disabled in this target to reduce receive latency; host enumeration
diagnostics and USB errors remain visible.

Sustained USB traffic, repeated hot reconnects, and full GP-5 operation still
require physical checks. Capture
the full serial log from attachment through sync or error, the generated
`sdkconfig.esp32-s3-n16r8`, and the pedal display response. Avoid capturing
Wi-Fi credentials in shared logs.
