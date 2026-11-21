# StompBridge

StompBridge is a ten-switch MIDI foot controller built around an ESP32-S3.
It controls a Sonicake Pocket Master or a Valeton GP-5 over USB and reads the
pedal's state back. An iPhone running Bluefy can serve as the controller's
screen and touch interface over Bluetooth Low Energy.

## Why this project
Im a software guy that wanted to try out embedded development with AI
i have two sons that are into music and have bought multiple pedals and midi controllers that never really work as good as you want them to because the midi controller does not know the effect units state
.
I saw other projects where people had 
* Built midi controllers
* Built custom patch editors that use Web bluetoth

and thought that the combo of a Stomp state aware midi foot controller with the ability to use an external screen was a fun and potentially usefull project to pursue.

the goal was to use the small budget stomps as if they were one of their Larger more expensive siblings.

## What the proof of concept does

| Part | Demonstrated behavior |
| --- | --- |
| Foot controller | Two bank switches, four preset switches, three effect switches, and a utility switch for the tuner and Bluetooth access. |
| Sonicake Pocket Master | USB preset selection and Drive, Delay, and Reverb control with confirmed readback. The board read all 100 preset names (P01–P50 and F01–F50) and decoded current effect models. |
| Valeton GP-5 | USB preset selection and Drive, Delay, and Reverb control with confirmed readback. The board read all 100 preset names (P00–P99) and decoded current effect models. |
| Phone screen | Bluefy on iPhone exercised live control with a GP-5. The board logged an encrypted, bonded session that stayed connected after the 60-second pairing window. |

The controller identifies one attached pedal at a time by its USB descriptor.
A bench test swapped between the two models without rebooting the controller,
and each new USB session synchronized. Two-way sync means the bridge sends live
commands and reads back what the pedal reports. Preset and effect changes
initiated on the bridge trigger readback. Continuous mirroring of every change
made on the pedal itself remains unverified, and tuner state is assumed because
neither pedal provides reliable tuner readback in this implementation. See the
[hardware evidence and limits](docs/support-matrix.md).

## Reproduce the POC

1. Wire the switches and the powered USB host port using the
   [hardware build guide](docs/hardware.md).
2. Set up PlatformIO and flash the BLE screen target through the board's
   separate USB-to-UART service port.

   ```sh
   ./scripts/setup-dev
   ./scripts/pio run -e stompbridge-next-ble-ui -t upload
   ./scripts/pio device monitor -e stompbridge-next-ble-ui -b 115200
   ```

3. Supply host VBUS and connect either pedal with a USB data cable. Look for
   its USB identity and a `SYNC` confirmation in the serial log. Press A or D
   to change presets, then try Drive, Delay, and Reverb. Compare the pedal
   display with the confirmed state in the log. The
   [USB bring-up guide](docs/usb-pedals.md) covers enumeration and recovery.
4. Open the hosted UI in Bluefy and hold **UTILITY** to authorize the phone.
   Follow the [phone screen guide](ui/README.md) for pairing, using your own
   HTTPS host, and checks to run on your setup.

## Firmware paths

| PlatformIO environment | Use |
| --- | --- |
| `stompbridge-next-ble-ui` | Full POC with USB pedal control, two-way readback, and the BLE phone screen. |
| `stompbridge-next-usb` | USB pedal and footswitch bring-up with serial logging. |
| `stompbridge-next-ui-dev` | Desktop UI development over Web Serial with a real USB pedal. |
| `stompbridge-next-input` | Switch-only diagnostic for wiring and debounce. |
| `esp32-s3-n16r8` | PlatformIO default. Earlier firmware with an ESP32-hosted Wi-Fi UI. |

## Project guide

- [Hardware](docs/hardware.md) covers the enclosure files, switch pinout, USB host wiring, and first power-up.
- [Bill of materials](docs/bom.md) lists what to source for one controller and what to have ready for testing.
- [Architecture](docs/architecture.md) shows how a switch press or phone command becomes MIDI and how confirmed pedal state returns to the screen.
- [Development](docs/development.md) covers local checks and the USB development UI.
- [Earlier prototypes](docs/legacy-prototypes.md) covers the ESP32 Wi-Fi interface, computer-hosted USB bridge, and BLE-MIDI experiments.

## Protocol acknowledgments

Thanks to the projects that helped us understand the two pedals' MIDI protocols.

- **Pocket Master Interoperability Guide**, by klotznick, documented the
  hardware-tested USB SysEx requests and response layouts used to read the
  current preset, live effect state, and preset directory.
  [Pocket Master Interoperability Guide](https://github.com/klotznick/pocket-master-interoperability-guide)
- **SonicMaster**, by Skyggedans and contributors, was a research reference
  for Pocket Master protocol framing over USB MIDI and BLE.
  [SonicMaster](https://github.com/Skyggedans/SonicMaster)
- **GP-5 Web Controller**, by Helvecio Neto and contributors, was a reference
  for the GP-5 MIDI CC map and SysEx messages. StompBridge checked USB readback
  on its own GP-5. [GP-5 Web Controller](https://github.com/helvecioneto/gp5-wc) ·
  [StompBridge GP-5 hardware capture](docs/gp5-patch-readout-2026-11-17.md)
- **pocket-master-mcp**, by voronkovd and contributors, supplied the Pocket
  Master effect-model ID-to-name mapping. That project credits **PocketEdit**,
  by suckyble and contributors, as the source for its model data.
  [pocket-master-mcp](https://github.com/voronkovd/pocket-master-mcp#credits-and-license) ·
  [PocketEdit](https://github.com/suckyble/PocketEdit)

## License

Original StompBridge code, documentation, and enclosure files are available
under the [Apache License 2.0](LICENSE). See [NOTICE](NOTICE) for project
attribution and the upstream Pocket Master model data provenance.
