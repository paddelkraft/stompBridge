# StompBridge hardware

The POC uses an ESP32-S3 and ten momentary footswitches in a printed enclosure.
The ESP32 hosts the effects pedal over USB. The separate USB-to-UART connector
is for flashing and logs, and the phone connects wirelessly.

## Parts and mechanical files

The [bill of materials](bom.md) lists quantities and compatibility checks for
the controller, USB host power, and enclosure.

The repository includes the [enclosure model](../MidiControllerEnclosure.sc),
[back hatch](../Back%20hatch.3mf), and
[USB attachment wedge](../usb%20attach%20wedge.3mf). Check the cutouts and fit
against your actual switches, breakout, and connectors before printing. The
files are prototype mechanical references.

## Switch layout and wiring

This is the reference layout viewed from above.

```text
Rear / connectors

  BANK− (4)      BANK+ (5)      UTILITY (13)
  DRIVE (6)      DELAY (7)      REVERB (8)

  A (9)          B (10)         C (11)        D (12)

Front / player
```

Wire one terminal of each switch to its assigned GPIO and the other to
common GND. The firmware enables the internal pull-up. A released switch
reads high, and pressing it pulls the input low. The numbers above are
GPIO numbers. The canonical pinout lives in [board_pins.h](../include/board_pins.h).

| Switch | GPIO | Diagnostic mask | POC behavior |
| --- | --- | --- | --- |
| BANK− / BANK+ | 4 / 5 | `0001` / `0002` | Move through local banks of four presets |
| DRIVE / DELAY / REVERB | 6 / 7 / 8 | `0004` / `0008` / `0010` | Toggle the pedal's corresponding effect block |
| A / B / C / D | 9 / 10 / 11 / 12 | `0020` / `0040` / `0080` / `0100` | Select a preset in the current bank |
| UTILITY | 13 | `0200` | Tap for tuner in the BLE build. Hold for Bluetooth access or revocation. |

Keep GPIO19 and GPIO20 for native USB. GPIO35–37 are used by the N16R8
module's octal PSRAM. The pin map reserves GPIO1 for expression, GPIO2 for
battery sensing, and GPIO17/18 for an OLED. Those peripherals are outside
this POC.

## USB host and power

| USB-A host signal | Connection |
| --- | --- |
| D− | GPIO19 |
| D+ | GPIO20 |
| GND | Common ground for the host port, ESP32, and external supply |
| VBUS | Protected, regulated external 5 V host supply |

In the bench setup, the USB-to-UART service cable powered the ESP32 and a
power bank supplied the host port's VBUS through a separate connection. The
service port alone did not provide usable VBUS to the pedal. Even an already
powered Pocket Master needed host VBUS to enumerate.

Use a current-limited VBUS path and keep the USB data pair short. Verify
source isolation before wiring an external supply to the ESP32's 5 V rail
while the service cable is attached. The GPIO pins carry the USB data signals.
Supply VBUS separately at 5 V.

## First power-up

1. With all power disconnected, check that each switch is open when released
   and connects only its assigned GPIO to GND when pressed. Check for shorts
   between neighboring terminals. Insulate unused conductors.
2. With only the service cable attached, flash the input diagnostic and open
   its serial monitor.

   ```sh
   ./scripts/pio run -e stompbridge-next-input -t upload
   ./scripts/pio device monitor -e stompbridge-next-input -b 115200
   ```

3. Tap each switch individually and compare the reported hexadecimal
   `mask` to the table. Expect `TAP` for either bank switch or UTILITY and
   `PRESS` for the other seven. Wiggle the harness and try a few two-switch
   combinations to catch loose wiring and shorts.
4. Close the monitor and flash the USB or BLE POC target. Connect the external
   host supply and check VBUS and polarity before attaching the pedal. If it
   fails to enumerate, measure VBUS under load and check that the cable carries
   data. See the [USB bring-up guide](usb-pedals.md).
