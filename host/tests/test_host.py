from __future__ import annotations

import struct
import unittest

from midimaster_host.controller import (
    Certainty,
    ControllerState,
    handle_press,
    handle_gp5_press,
    parse_switch_event,
    preset_label,
)
from midimaster_host.gp5_protocol import (
    decode_current_preset as decode_gp5_current_preset,
    decode_patch_state_fragment,
)
from midimaster_host.protocol import (
    FragmentCollector,
    ProtocolError,
    crc8,
    decode_current_preset,
    decode_global_settings,
    decode_live_state,
    decode_sysex_data,
)


def encode_frame(command: int, index: int, payload: bytes) -> bytes:
    body = bytearray([0, command, index, len(payload)]) + bytearray(payload)
    body[0] = crc8(body)
    return bytes(part for byte in body for part in (byte >> 4, byte & 0x0F))


def tlv(identifier: int, value: bytes) -> bytes:
    return struct.pack("<HH", identifier, len(value)) + value


def u32(value: int) -> bytes:
    return struct.pack("<I", value)


class ProtocolTests(unittest.TestCase):
    def test_known_request_decodes(self) -> None:
        command, index, payload = decode_sysex_data(
            bytes.fromhex("0B 09 00 01 00 00 00 02 01 02 01 00")
        )
        self.assertEqual((command, index, payload), (1, 0, b"\x12\x10"))

    def test_crc_and_length_are_validated(self) -> None:
        valid = encode_frame(4, 0, b"\x12\x10")
        with self.assertRaises(ProtocolError):
            decode_sysex_data(bytes([valid[0] ^ 1]) + valid[1:])
        with self.assertRaises(ProtocolError):
            decode_sysex_data(valid[:-1])

    def test_fragment_collector_is_bounded_and_ordered(self) -> None:
        collector = FragmentCollector(4, 2, b"\x12\x10", 4)
        self.assertIsNone(collector.accept(4, 1, b"stale tail"))
        self.assertIsNone(collector.accept(4, 0, b"\x12\x10"))
        self.assertEqual(collector.accept(4, 1, b"\x01\x02"), b"\x12\x10\x01\x02")

    def test_fragment_order_is_strict_after_start(self) -> None:
        collector = FragmentCollector(4, 3, b"\x12", 3)
        self.assertIsNone(collector.accept(4, 0, b"\x12"))
        with self.assertRaises(ProtocolError):
            collector.accept(4, 2, b"\x00")

    def test_current_preset_range(self) -> None:
        self.assertEqual(decode_current_preset(b"\x12\x43\x63\x00"), 99)
        with self.assertRaises(ProtocolError):
            decode_current_preset(b"\x12\x43\x64\x00")

    def test_live_state_tlv_decode(self) -> None:
        format_section = tlv(1, u32(2)) + tlv(2, b"\x0AEMQ")
        layout_section = tlv(0x1001, u32(10)) + tlv(0x1002, u32(8)) + tlv(0x1003, u32(2))
        global_section = tlv(0x2001, u32(73)) + tlv(0x2002, u32(121))
        module_section = (
            tlv(0x3001, u32((1 << 2) | (1 << 8)))
            + tlv(0x3002, bytes(range(10)))
            + tlv(0x3003, b"".join(u32(i) for i in range(10)))
            + tlv(0x3004, bytes(320))
        )
        logical = b"\x12\x41" + (
            tlv(0x00FF, format_section)
            + tlv(0x0000, layout_section)
            + tlv(0x0001, global_section)
            + tlv(0x0002, module_section)
            + tlv(0x0003, bytes(8))
        )
        self.assertEqual(len(logical), 476)
        state = decode_live_state(logical, 6)
        self.assertEqual((state.preset_index, state.preset_volume, state.tempo_bpm), (6, 73, 121))
        self.assertTrue(state.modules["DRV"])
        self.assertTrue(state.modules["RVB"])
        self.assertFalse(state.modules["DLY"])

    def test_global_settings_decode(self) -> None:
        fields = (
            tlv(0x0101, bytes([3, 3, 1, 1]))
            + tlv(0x0102, u32(2))
            + tlv(0x0201, b"\x01")
            + tlv(0x0202, u32(77))
            + tlv(0x0203, b"\x64")
            + tlv(0x0205, b"\x01")
            + tlv(0x0301, b"\xF6")
            + tlv(0x0401, b"\x00")
            + tlv(0x0402, b"\xEC")
            + tlv(0x0403, b"\x01")
            + tlv(0x0404, b"\x01")
            + tlv(0x0405, b"\x00")
        )
        logical = b"\x12\x10" + fields
        self.assertEqual(len(logical), 71)
        settings = decode_global_settings(logical)
        self.assertEqual((settings.firmware, settings.tuner_flag, settings.master_volume), ("V1.3.3", True, 77))


class ControllerTests(unittest.TestCase):
    def test_serial_event_parser(self) -> None:
        event = parse_switch_event("SW3 DRIVE   IO6 PRESSED")
        self.assertEqual((event.name, event.pin, event.pressed), ("DRIVE", 6, True))
        self.assertIsNone(parse_switch_event("startup banner"))

    def test_bank_and_slot_select_absolute_preset(self) -> None:
        state = ControllerState()
        handle_press(state, "BANK+")
        command = handle_press(state, "C")
        self.assertEqual((command.control, command.value), (1, 7))
        self.assertEqual(state.preset.certainty, Certainty.ASSUMED)
        self.assertEqual(preset_label(state.preset.value), "P07")

    def test_bank_is_clamped_to_100_presets(self) -> None:
        state = ControllerState(bank=24)
        handle_press(state, "BANK+")
        command = handle_press(state, "D")
        self.assertEqual(command.value, 100)
        self.assertEqual(preset_label(99), "F50")

    def test_effect_defaults_on_then_toggles_assumed_state(self) -> None:
        state = ControllerState()
        first = handle_press(state, "DELAY")
        second = handle_press(state, "DELAY")
        self.assertEqual((first.control, first.value), (50, 127))
        self.assertEqual(second.value, 0)

    def test_gp5_mapping_is_isolated(self) -> None:
        state = ControllerState(bank=1)
        preset = handle_gp5_press(state, "C")
        drive = handle_gp5_press(state, "DRIVE")
        delay = handle_gp5_press(state, "DELAY")
        reverb = handle_gp5_press(state, "REVERB")
        self.assertEqual((preset.control, preset.value), (0, 6))
        self.assertEqual((drive.control, delay.control, reverb.control), (50, 56, 57))


class Gp5ProtocolTests(unittest.TestCase):
    def test_current_preset_response(self) -> None:
        body = bytes.fromhex("05 0e 00 01 00 00 00 04 01 02 04 03 00 06 00 00")
        self.assertEqual(decode_gp5_current_preset(body), 6)

    def test_patch_state_fragment(self) -> None:
        body = bytearray(46)
        body[2:6] = bytes.fromhex("01 09 00 03")
        body[34:38] = bytes((0x08, 0x04, 0x00, 0x01))
        modules = decode_patch_state_fragment(bytes(body))
        self.assertEqual(modules, {"DRIVE": True, "DELAY": True, "REVERB": True})
        body[5] = 2
        self.assertIsNone(decode_patch_state_fragment(bytes(body)))


if __name__ == "__main__":
    unittest.main()
