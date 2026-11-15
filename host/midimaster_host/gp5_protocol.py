"""Bounded, read-only Valeton GP-5 USB-MIDI state decoding.

The GP-5 uses nibble-valued SysEx bodies for preset and patch-state reads.
This module deliberately contains no preset-save or editor-write protocol.
"""

from __future__ import annotations

from dataclasses import dataclass

from .protocol import ProtocolError


CURRENT_PRESET_REQUEST = bytes.fromhex("00 07 00 01 00 00 00 02 01 02 04 03")
PATCH_STATE_REQUEST = bytes.fromhex("00 09 00 01 00 00 00 02 01 02 04 01")


@dataclass(frozen=True)
class Gp5State:
    preset_index: int
    modules: dict[str, bool]


def decode_current_preset(body: bytes) -> int:
    """Decode the body between F0/F7 of an 18-byte preset response."""
    if len(body) != 16:
        raise ProtocolError(f"GP-5 preset response has {len(body)} body bytes; expected 16")
    if body[2:12] != bytes.fromhex("00 01 00 00 00 04 01 02 04 03"):
        raise ProtocolError("unexpected GP-5 preset response signature")
    if body[14:16] != b"\x00\x00":
        raise ProtocolError("unexpected GP-5 preset response trailer")
    high, low = body[12:14]
    if high > 0x0F or low > 0x0F:
        raise ProtocolError("GP-5 preset response contains a non-nibble preset")
    preset = (high << 4) | low
    if preset > 99:
        raise ProtocolError(f"GP-5 preset index {preset} outside 0..99")
    return preset


def decode_patch_state_fragment(body: bytes) -> dict[str, bool] | None:
    """Decode effect states from fragment 3 of a GP-5 patch dump.

    Normal fragments have 46 body bytes (48 including F0/F7). Fragment 3
    stores a little-endian status bitmap at full-message bytes 35..38.
    """
    if len(body) != 46:
        return None
    if body[2:6] != bytes.fromhex("01 09 00 03"):
        return None
    status = int.from_bytes(body[34:38], "little")
    return {
        "DRIVE": bool(status & (1 << 10)),
        "DELAY": bool(status & (1 << 3)),
        "REVERB": bool(status & (1 << 24)),
    }
