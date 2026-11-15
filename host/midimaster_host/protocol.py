"""Bounded Pocket Master USB-MIDI framing and read-only state decoding.

Protocol constants and layouts are based on hardware-tested observations from
the CC0 Pocket Master Interoperability Guide. This module intentionally
contains no vendor-specific write or preset-save commands.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass


MODULE_NAMES = ("NR", "FX1", "DRV", "AMP", "IR", "EQ", "FX2", "DLY", "RVB", "CLONE")

# Data bytes between F0/F7. Mido adds/removes the SysEx boundary bytes.
CURRENT_PRESET_REQUEST = bytes.fromhex("00 07 00 01 00 00 00 02 01 02 04 03")
LIVE_STATE_REQUEST = bytes.fromhex("00 09 00 01 00 00 00 02 01 02 04 01")
GLOBAL_SETTINGS_REQUEST = bytes.fromhex("0B 09 00 01 00 00 00 02 01 02 01 00")


class ProtocolError(RuntimeError):
    """A malformed, incomplete, or semantically unexpected device message."""


def crc8(data: bytes | bytearray) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def decode_sysex_data(data: bytes) -> tuple[int, int, bytes]:
    """Decode one nibble-expanded Pocket Master SysEx body from Mido."""
    if len(data) % 2:
        raise ProtocolError("odd number of encoded nibbles")
    if any(byte > 0x0F for byte in data):
        raise ProtocolError("non-nibble byte inside SysEx body")

    body = bytes((data[i] << 4) | data[i + 1] for i in range(0, len(data), 2))
    if len(body) < 4:
        raise ProtocolError("decoded body is shorter than four bytes")
    if body[3] != len(body) - 4:
        raise ProtocolError("payload length does not match frame header")
    if body[0] != crc8(bytes([0]) + body[1:]):
        raise ProtocolError("CRC mismatch")
    return body[1], body[2], body[4:]


def parse_tlvs(data: bytes) -> dict[int, bytes]:
    fields: dict[int, bytes] = {}
    offset = 0
    while offset < len(data):
        if offset + 4 > len(data):
            raise ProtocolError(f"truncated TLV header at offset {offset}")
        identifier, length = struct.unpack_from("<HH", data, offset)
        start = offset + 4
        end = start + length
        if end > len(data):
            raise ProtocolError(f"TLV {identifier:04X} exceeds available data")
        if identifier in fields:
            raise ProtocolError(f"duplicate TLV {identifier:04X}")
        fields[identifier] = data[start:end]
        offset = end
    return fields


class FragmentCollector:
    """Strictly collect one known multipart response in index order."""

    def __init__(self, command: int, parts: int, prefix: bytes, length: int):
        self.command = command
        self.parts = parts
        self.prefix = prefix
        self.length = length
        self._payloads: dict[int, bytes] = {}

    def accept(self, command: int, index: int, payload: bytes) -> bytes | None:
        if command != self.command:
            return None
        if not 0 <= index < self.parts:
            raise ProtocolError(f"fragment index {index} outside response")
        # The pedal can finish an unsolicited state dump immediately before a
        # requested one. A tail fragment cannot begin a new response, so ignore
        # it until fragment zero establishes a boundary. Ordering remains
        # strict after collection has started.
        if not self._payloads and index != 0:
            return None
        if index in self._payloads:
            raise ProtocolError(f"duplicate fragment {index}")
        if index != len(self._payloads):
            raise ProtocolError(f"out-of-order fragment {index}; expected {len(self._payloads)}")
        self._payloads[index] = payload
        if len(self._payloads) != self.parts:
            return None
        logical = b"".join(self._payloads[i] for i in range(self.parts))
        if len(logical) != self.length or not logical.startswith(self.prefix):
            raise ProtocolError(
                f"unexpected response: {len(logical)} bytes, prefix {logical[:2].hex(' ')}"
            )
        return logical


@dataclass(frozen=True)
class LiveState:
    preset_index: int
    preset_volume: int
    tempo_bpm: int
    modules: dict[str, bool]
    chain: tuple[str, ...]


@dataclass(frozen=True)
class GlobalSettings:
    firmware: str | None
    # Tag 0201 is statically associated with the tuner in community notes, but
    # did not follow the visible live tuner state in our USB test. Preserve the
    # raw flag without presenting it as confirmed tuner state.
    tuner_flag: bool | None
    master_volume: int | None


def decode_current_preset(logical: bytes) -> int:
    if len(logical) != 4 or not logical.startswith(b"\x12\x43"):
        raise ProtocolError("invalid current-preset response")
    index = int.from_bytes(logical[2:4], "little")
    if not 0 <= index < 100:
        raise ProtocolError(f"current preset index {index} outside 0..99")
    return index


def _required(fields: dict[int, bytes], tag: int, length: int | None = None) -> bytes:
    if tag not in fields:
        raise ProtocolError(f"required TLV {tag:04X} is missing")
    value = fields[tag]
    if length is not None and len(value) != length:
        raise ProtocolError(f"TLV {tag:04X} has {len(value)} bytes; expected {length}")
    return value


def decode_live_state(logical: bytes, preset_index: int) -> LiveState:
    if len(logical) != 476 or not logical.startswith(b"\x12\x41"):
        raise ProtocolError("invalid live-state response")
    sections = parse_tlvs(logical[2:])
    global_fields = parse_tlvs(_required(sections, 0x0001))
    module_fields = parse_tlvs(_required(sections, 0x0002))

    preset_volume = int.from_bytes(_required(global_fields, 0x2001, 4), "little")
    tempo_bpm = int.from_bytes(_required(global_fields, 0x2002, 4), "little")
    enable_mask = int.from_bytes(_required(module_fields, 0x3001, 4), "little")
    chain_ids = _required(module_fields, 0x3002, 10)
    if sorted(chain_ids) != list(range(10)):
        raise ProtocolError("module chain is not a permutation of 0..9")

    modules = {name: bool(enable_mask & (1 << i)) for i, name in enumerate(MODULE_NAMES)}
    chain = tuple(MODULE_NAMES[i] for i in chain_ids)
    return LiveState(preset_index, preset_volume, tempo_bpm, modules, chain)


def decode_global_settings(logical: bytes) -> GlobalSettings:
    if len(logical) != 71 or not logical.startswith(b"\x12\x10"):
        raise ProtocolError("invalid global-settings response")
    fields = parse_tlvs(logical[2:])

    firmware = None
    raw_firmware = fields.get(0x0101)
    if raw_firmware is not None and len(raw_firmware) == 4:
        prefix = "B" if raw_firmware[3] == 0 else "V"
        firmware = f"{prefix}{raw_firmware[2]}.{raw_firmware[1]}.{raw_firmware[0]}"

    raw_tuner = fields.get(0x0201)
    tuner_flag = bool(raw_tuner[0]) if raw_tuner is not None and len(raw_tuner) == 1 else None
    raw_volume = fields.get(0x0202)
    master_volume = (
        int.from_bytes(raw_volume, "little", signed=True)
        if raw_volume is not None and len(raw_volume) == 4
        else None
    )
    return GlobalSettings(firmware, tuner_flag, master_volume)


def decode_module_feedback(logical: bytes) -> tuple[str, bool] | None:
    """Decode confirmed `12 49` original-module enable feedback."""
    if len(logical) != 10 or not logical.startswith(b"\x12\x49"):
        return None
    module_id = logical[2]
    if module_id >= 9:
        return None
    return MODULE_NAMES[module_id], logical[6] != 0
