# StompBridge bill of materials

Quantities are for one ten-switch controller. The list specifies compatible
parts by fit and function. Check dimensions against your board and enclosure
before ordering. See the [hardware guide](hardware.md) for the pinout and
assembly checks.

## Controller and USB host

| Qty | Part | Specification |
| ---: | --- | --- |
| 1 | ESP32-S3 N16R8 development board | 16 MB flash, 8 MB octal PSRAM, a separate USB-to-UART service port, and accessible GPIO4–13 and GPIO19/20. See the [PlatformIO board definition](../boards/esp32-s3-n16r8.json). |
| 1 | Screw-terminal breakout | Match the development board's pin count, header spacing, and terminal labels. Verify pin alignment before inserting the board. |
| 10 | Panel-mount footswitches | Two-terminal, normally open, momentary. Check the threaded bushing, panel thickness, and clearance below the switch. |
| 1 | USB-A female host breakout | USB 2.0 data with accessible D−, D+, GND, and VBUS. Use a connector or bracket that can be secured to the enclosure. |
| 1 | VBUS protection stage | A 5 V current-limited USB power switch or a host power module with this function built in. Follow the module's wiring and capacitor requirements. |
| 1 | Regulated 5 V host supply | A USB power bank or bench supply with enough current for the attached pedal. The tested bench setup used a separate supply for host VBUS. |
| 1 | Host supply lead | Match the supply and 5 V input connection. A USB power bank needs a suitable 5 V input breakout or lead for the power stage. |
| 1 | Pedal USB data cable | USB-A to USB-C for the Pocket Master or GP-5. |
| 1 | Service USB data cable | Match the computer and the ESP32-S3's USB-to-UART port. Used for flashing, serial logs, and bench power to the ESP32. |
| 1 set | Internal wiring and termination | Ten GPIO signal runs, shared ground wiring, a short USB data pair, and 5 V/GND leads. Include heat-shrink and ferrules sized for the wires and screw terminals. |

## Enclosure and mounting

| Qty | Part | Specification |
| ---: | --- | --- |
| 1 set | Printed enclosure and connector mounts | Start with the [enclosure model](../MidiControllerEnclosure.sc), [back hatch](../Back%20hatch.3mf), and [USB attachment wedge](../usb%20attach%20wedge.3mf). Check fit with the actual breakout, switches, and USB connector. |
| 1 set | Mounting hardware | Switch nuts and washers, board standoffs, enclosure fasteners, and cable strain relief. Size and count depend on the selected parts and print. |
| 1 each, optional | 6-way and 8-way polarized locking connector pairs | For removable switch panels. The front panel carries four switch signals and ground. The upper panel carries six switch signals and ground. Include mating housings and contacts. |

## Bring-up and demonstration

| Qty | Item | Use |
| ---: | --- | --- |
| 1 | Computer with USB and PlatformIO | Build, flash, and read the ESP32 serial log. |
| 1 | Multimeter | Check switch continuity and host VBUS before connecting a pedal. |
| 1 | Sonicake Pocket Master or Valeton GP-5 | Either pedal can demonstrate USB control and readback. Use one of each to reproduce the pedal-swap check. |
| 1, optional | iPhone with Bluefy | Use with the [phone screen guide](../ui/README.md) to demonstrate BLE control. The recorded phone trial used a GP-5. |

The bench build powers the ESP32 through the service cable and the pedal
through the protected USB host rail. For a standalone build that powers both
from one power bank, budget a 5 V distribution stage and verify source
isolation on the ESP32 branch before attaching a powered service cable.
An OLED, expression input, and internal rechargeable battery are future
build options.
