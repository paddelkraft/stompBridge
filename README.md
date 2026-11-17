# StompBridge

StompBridge is a two-way MIDI foot controller built around a battery-powered,
10-footswitch ESP32-S3. It sends live performance controls and reads pedal
state back for synchronization. Initial target devices are the Sonicake
Pocket Master and Valeton GP-5.

The current firmware combines the debounced switch diagnostic with selectable
BLE-MIDI and USB-MIDI transports. Input and pedal mapping code depend only on
the shared `MidiTransport` interface, so transport selection does not change
the generated MIDI messages. The default firmware uses USB-MIDI host mode and selects the Pocket Master
or GP-5 plugin using the connected pedal’s USB ID. The firmware sends only
live-performance commands and read-only synchronization requests; it contains
no preset-save or editor-write commands.

## HTML UI prototype

An interactive, dependency-free control surface prototype lives in [`ui/`](ui/).
Open `ui/index.html` directly or run:

```sh
python3 -m http.server 8080 --directory ui
```

The same UI is embedded into the ESP32 firmware as gzip-compressed assets. On
startup the controller creates a board-specific WPA2 network and prints its
credentials to the serial monitor:

```sh
./scripts/pio run --target upload
./scripts/pio device monitor
```

Connect a phone or tablet to the printed `StompBridge-xxxx` network, then open
<http://192.168.4.1/>. The UI reads live controller state and sends semantic
actions through the same bounded queue used by the physical footswitches. UI
source changes are embedded automatically on the next PlatformIO build.

## Computer-hosted USB prototype

While the controller's protected USB-host hardware is being built, a computer
can host both connections:

```text
footswitches -> ESP32 USB-UART -> host app -> Pocket Master USB-MIDI
                                      ^             |
                                      +-- state <---+
```

This keeps both standard MIDI control and confirmed pedal-state reads on USB;
BLE remains available for other uses. The prototype contains no preset-save,
rename, import, or other persistent-write commands.

Set up its isolated Python environment:

```sh
./scripts/setup-host
```

With the Pocket Master connected by USB and other MIDI editors closed, list
the ALSA card and raw-MIDI device:

```sh
./scripts/midimaster-host ports
```

On Linux, the account must have access to ALSA devices. If the command reports
`Permission denied`, add the account to the `audio` group and then sign out and
back in:

```sh
sudo usermod -aG audio "$USER"
```

For a temporary test that lasts only until the pedal is disconnected, grant an
ACL to the currently enumerated endpoint instead:

```sh
sudo setfacl -m u:"$USER":rw /dev/snd/midiC2D0
```

Perform a read-only firmware/current-preset/live-state check:

```sh
./scripts/midimaster-host read-state --midi-device /dev/snd/midiC2D0
```

For a Valeton GP-5, select its isolated device profile:

```sh
./scripts/midimaster-host read-state --device-profile gp5 \
  --midi-device /dev/snd/midiC2D0
./scripts/midimaster-host run --device-profile gp5 --serial /dev/ttyACM0 \
  --midi-device /dev/snd/midiC2D0
```

The GP-5 profile reads its current preset and patch bitmap over USB. Its
separate mapping uses CC0 for absolute presets, CC50/56/57 for Drive/Delay/
Reverb, and CC58 for the tuner. It contains no preset-save/editor writes.
Hardware validation on a GP-5 confirmed P01-P08 absolute preset selection and
Drive/Delay/Reverb switching with USB state readback. CC58 visibly controls the
tuner, whose state remains locally assumed because no reliable readback has
been identified.

Run the footswitch bridge, substituting the exact port names printed above:

```sh
./scripts/midimaster-host run --serial /dev/ttyACM0 \
  --midi-device /dev/snd/midiC2D0
```

The host maps A-D to four absolute presets in the selected bank, Drive/Delay/
Reverb to their documented module CCs, and Utility to the tuner. It reads state
back after commands and labels values as confirmed, assumed, or unknown.

Hardware validation on Pocket Master firmware V1.3.3 confirmed preset selection
and Drive/Delay/Reverb control with USB state readback. Utility's documented
CC58 also switched the tuner on physically. The global-settings `0201` flag did
not follow that visible state, so tuner state is tracked as locally assumed—not
USB-confirmed. Supply the visible startup state when known:

```sh
./scripts/midimaster-host run --serial /dev/ttyACM0 \
  --midi-device /dev/snd/midiC2D0 --tuner-starting on
```

## Hardware baseline

- ESP32-S3 N16R8 development board
- Matching parallel-header/screw-terminal breakout
- Ten two-terminal, normally-open momentary footswitches
- Switch signal terminals: IO4 through IO13
- All other switch terminals: common GND

Project planning notes and hardware reference photos are kept in the local-only
`local-only/` folder and are excluded from version control.

## Local toolchain

The project uses PlatformIO Core installed in the project-local `.venv`.
Downloaded compiler/framework packages are also isolated under `.platformio`.

```sh
./scripts/pio run
```

Replacement development is isolated in the `stompbridge-next-input` build.
It currently exercises the new switch gesture core as a serial diagnostic;
it does not connect to a pedal or serve the web UI. Compile it without
uploading with `./scripts/pio run -e stompbridge-next-input`. The separate
`stompbridge-next-usb` target compiles the experimental USB session adapter;
it automatically sends read-only synchronization requests after verified USB
descriptor identification and routes the board switches to live commands. It
has no web UI. Compile it with `./scripts/pio run -e stompbridge-next-usb`.
GP-5 sync, effect models, and all 100 preset names were confirmed on hardware
in this experimental target; Pocket Master has not had the same physical
validation in this target. The default
`esp32-s3-n16r8` build remains the working firmware until replacement
acceptance. See [development checks](docs/development.md) and the
[architecture progress](docs/architecture.md).

Upload through the board's USB-to-UART connector:

```sh
./scripts/pio run --target upload
```

Open the serial diagnostic at 115200 baud:

```sh
./scripts/pio device monitor
```

Exit the monitor with `Ctrl-C`.

## Standalone USB pedal setup

See the [USB pedal guide](docs/usb-pedals.md) for wiring, host power, firmware
settings, and serial troubleshooting for both pedals.

## Embedded Valeton GP-5 USB-host test

The current embedded test maps the switches as follows:

- `BANK-` / `BANK+`: move among 25 local four-preset banks;
- `A`-`D`: select the four GP-5 presets in the active bank;
- `DRIVE`, `DELAY`, `REVERB`: toggle the GP-5 DST, DLY, and RVB blocks;
- `UTILITY`: toggle the tuner with CC58.

Upload through the USB-to-UART service connector, leave the serial monitor on,
and connect the powered GP-5 to the protected host connector wired to IO19
(`D-`) and IO20 (`D+`). A successful enumeration prints `USB-MIDI connected`
with its vendor/product IDs. The firmware then requests GP-5 patch names,
current preset, and effect-block state over read-only SysEx. Toggle state is
initially local and changes to confirmed when readback arrives.

## Embedded Pocket Master BLE-MIDI test

The ESP32-S3 acts as the BLE central/client and scans for the standard BLE-MIDI
service exposed by the pedal. Put the Pocket Master into its Bluetooth MIDI
advertising/pairing mode, then reset StompBridge. The serial log progresses
through `BLE-MIDI scanning`, `BLE-MIDI service found`, and `BLE-MIDI connected`.
The same A-D and effect mappings above are then sent through BLE without any
changes to the switch or Pocket Master profile code.

## Footswitch hardware test

1. For a switch-only test, leave the battery, pedal USB, and expression jack
   disconnected. Power only through the board's USB-to-UART/programming
   connector.
2. With power disconnected, use continuity mode to confirm every switch is
   open when released and connects only its assigned GPIO to GND when pressed.
3. Insulate every unused or loose conductor so it cannot touch another terminal.
4. Upload the firmware and open the serial monitor.
5. Confirm the startup line reports `Flash: 16 MB` and all ten
   initial states are `RELEASED`. `PRESSED/CHECK WIRING` indicates a held switch
   or a GPIO-to-ground short.
6. Press and release each switch once in the order below. Expect exactly one
   `PRESSED` and one `RELEASED` line with the matching name and GPIO:

   `BANK- IO4`, `BANK+ IO5`, `DRIVE IO6`, `DELAY IO7`, `REVERB IO8`,
   `A IO9`, `B IO10`, `C IO11`, `D IO12`, `UTILITY IO13`.

7. Hold each switch for two seconds and gently move its wire near both ends.
   There must be no extra events. Then test a few two-switch combinations to
   catch accidental shorts between signal wires.

The project configuration uses GPIO19/20 for native USB-host MIDI and
does not use GPIO35/36/37, which are occupied by the N16R8 module's octal PSRAM.
