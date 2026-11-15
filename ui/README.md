# StompBridge HTML UI prototype

This is a dependency-free, interactive prototype of the controller UI. It
simulates device selection, connection state, bank and preset navigation,
effect toggles, tuner mode, state certainty, and an activity log.

Open `index.html` directly, or serve the folder locally:

```sh
python3 -m http.server 8080 --directory ui
```

Then visit <http://localhost:8080>.

Keyboard shortcuts: `1`/`2`/`3` toggle Drive, Delay, and Reverb; `T` opens the
tuner; `A`–`D` select presets; Left/Right changes bank.

When opened from the ESP32 at <http://192.168.4.1/>, the UI automatically uses
the firmware's `/api/v1/state` and `/api/v1/action` endpoints. When opened as a
standalone file or from a development server, it falls back to simulation.

PlatformIO runs `scripts/embed_web_ui.py` before each firmware build to gzip
these files into `include/web_ui_assets.h`; no separate filesystem upload is
needed.
