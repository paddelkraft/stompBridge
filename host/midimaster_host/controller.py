"""Footswitch parsing and isolated pedal action mappings."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from enum import Enum


SWITCH_LINE = re.compile(
    r"^SW(?P<number>\d+)\s+(?P<name>BANK-|BANK\+|DRIVE|DELAY|REVERB|A|B|C|D|UTILITY)"
    r"\s+IO(?P<pin>\d+)\s+(?P<edge>PRESSED|RELEASED)$"
)


class Certainty(str, Enum):
    UNKNOWN = "unknown"
    ASSUMED = "assumed"
    CONFIRMED = "confirmed"


@dataclass
class Value:
    value: object | None = None
    certainty: Certainty = Certainty.UNKNOWN


@dataclass
class ControllerState:
    bank: int = 0
    preset: Value = field(default_factory=Value)
    modules: dict[str, Value] = field(
        default_factory=lambda: {
            name: Value() for name in ("DRIVE", "DELAY", "REVERB")
        }
    )
    tuner: Value = field(default_factory=Value)


@dataclass(frozen=True)
class SwitchEvent:
    name: str
    pin: int
    pressed: bool


@dataclass(frozen=True)
class MidiCommand:
    control: int
    value: int
    description: str
    refresh: str


def parse_switch_event(line: str) -> SwitchEvent | None:
    match = SWITCH_LINE.match(line.strip())
    if not match:
        return None
    return SwitchEvent(
        name=match.group("name"),
        pin=int(match.group("pin")),
        pressed=match.group("edge") == "PRESSED",
    )


def handle_press(state: ControllerState, name: str) -> MidiCommand | None:
    if name == "BANK-":
        state.bank = max(0, state.bank - 1)
        return None
    if name == "BANK+":
        state.bank = min(24, state.bank + 1)
        return None
    if name in "ABCD" and len(name) == 1:
        slot = "ABCD".index(name)
        preset_index = state.bank * 4 + slot
        state.preset = Value(preset_index, Certainty.ASSUMED)
        return MidiCommand(1, preset_index + 1, f"select preset {preset_label(preset_index)}", "live")

    effect_map = {
        "DRIVE": ("DRIVE", 45),
        "DELAY": ("DELAY", 50),
        "REVERB": ("REVERB", 51),
    }
    if name in effect_map:
        state_name, control = effect_map[name]
        current = state.modules[state_name]
        target = not bool(current.value) if current.certainty != Certainty.UNKNOWN else True
        state.modules[state_name] = Value(target, Certainty.ASSUMED)
        return MidiCommand(control, 127 if target else 0, f"{state_name} {'on' if target else 'off'}", "live")

    if name == "UTILITY":
        target = not bool(state.tuner.value) if state.tuner.certainty != Certainty.UNKNOWN else True
        state.tuner = Value(target, Certainty.ASSUMED)
        # CC58 control is hardware-confirmed, but no reliable live USB tuner
        # readback has been identified. Keep this state explicitly assumed.
        return MidiCommand(58, 127 if target else 0, f"tuner {'on' if target else 'off'}", "none")
    return None


def handle_gp5_press(state: ControllerState, name: str) -> MidiCommand | None:
    """Map one press using only the GP-5's documented standard MIDI CCs."""
    if name == "BANK-":
        state.bank = max(0, state.bank - 1)
        return None
    if name == "BANK+":
        state.bank = min(24, state.bank + 1)
        return None
    if name in "ABCD" and len(name) == 1:
        slot = "ABCD".index(name)
        preset_index = state.bank * 4 + slot
        state.preset = Value(preset_index, Certainty.ASSUMED)
        return MidiCommand(0, preset_index, f"select GP-5 preset {gp5_preset_label(preset_index)}", "live")

    effect_map = {"DRIVE": 50, "DELAY": 56, "REVERB": 57}
    if name in effect_map:
        current = state.modules[name]
        target = not bool(current.value) if current.certainty != Certainty.UNKNOWN else True
        state.modules[name] = Value(target, Certainty.ASSUMED)
        return MidiCommand(
            effect_map[name], 127 if target else 0,
            f"{name} {'on' if target else 'off'}", "live"
        )

    if name == "UTILITY":
        target = not bool(state.tuner.value) if state.tuner.certainty != Certainty.UNKNOWN else True
        state.tuner = Value(target, Certainty.ASSUMED)
        return MidiCommand(58, 127 if target else 0, f"tuner {'on' if target else 'off'}", "none")
    return None


def preset_label(index: int) -> str:
    return f"P{index + 1:02d}" if index < 50 else f"F{index - 49:02d}"


def gp5_preset_label(index: int) -> str:
    return f"P{index + 1:02d}"
