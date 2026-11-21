# Earlier StompBridge prototypes

The [POC walkthrough](../README.md) uses the ESP32 as a standalone USB host
and Bluefy as the BLE screen. These earlier paths are useful for comparison
and diagnostics.

## ESP32-hosted Wi-Fi UI

The default `esp32-s3-n16r8` environment embeds the static UI as gzip assets.
Flash through the USB-to-UART service port and open the serial monitor to see
the credentials for the board-specific `StompBridge-xxxx` WPA2 network.
Join that network and open <http://192.168.4.1/>. This route uses the legacy
HTTP state/action adapter. PlatformIO embeds changes to `ui/` on the next
build.

## Computer-hosted USB prototype

For bench testing the pedal protocol before assembling the standalone USB
host hardware, a computer can host both connections.

```text
footswitches -> ESP32 USB-UART -> host app -> Pocket Master USB-MIDI
                                      ^             |
                                      +-- state <---+
```

The host sends live MIDI and reads pedal state over USB.

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
Reverb, and CC58 for the tuner.
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
CC58 also switched the tuner on physically. The global-settings `0201` flag
stayed unchanged, so the host tracks tuner state as assumed. Supply the visible
startup state when known.

```sh
./scripts/midimaster-host run --serial /dev/ttyACM0 \
  --midi-device /dev/snd/midiC2D0 --tuner-starting on
```

## Embedded Pocket Master BLE-MIDI experiment

The code can act as a BLE central and discover the standard BLE-MIDI service.
The original experiment used the Pocket Master's Bluetooth MIDI advertising
mode. Automatic plugin selection requires verified pedal identity, and the
BLE-MIDI service UUID alone cannot supply it. Embedded pedal control on this
path remains unverified. See the [support matrix](support-matrix.md).
