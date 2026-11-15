"""CLI for the computer-hosted StompBridge USB prototype."""

from __future__ import annotations

import argparse
import glob
import os
import select
import sys
import termios
import time
from typing import Any

from .controller import (
    Certainty,
    ControllerState,
    gp5_preset_label,
    handle_gp5_press,
    handle_press,
    parse_switch_event,
    preset_label,
)
from .gp5_protocol import (
    CURRENT_PRESET_REQUEST as GP5_CURRENT_PRESET_REQUEST,
    PATCH_STATE_REQUEST as GP5_PATCH_STATE_REQUEST,
    Gp5State,
    decode_current_preset as decode_gp5_current_preset,
    decode_patch_state_fragment,
)
from .protocol import (
    CURRENT_PRESET_REQUEST,
    GLOBAL_SETTINGS_REQUEST,
    LIVE_STATE_REQUEST,
    FragmentCollector,
    ProtocolError,
    decode_current_preset,
    decode_global_settings,
    decode_live_state,
    decode_module_feedback,
    decode_sysex_data,
)


class RawMidiPort:
    """Small nonblocking ALSA raw-MIDI byte-stream adapter."""

    def __init__(self, path: str):
        self.path = path
        self.fd: int | None = None
        self._sysex: bytearray | None = None
        self._completed: list[bytes] = []

    def __enter__(self) -> "RawMidiPort":
        self.fd = os.open(self.path, os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY)
        self.drain()
        return self

    def __exit__(self, *args: object) -> None:
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def _write(self, data: bytes) -> None:
        view = memoryview(data)
        while view:
            try:
                written = os.write(self.fd, view)
                view = view[written:]
            except BlockingIOError:
                select.select([], [self.fd], [], 0.1)

    def send_cc(self, control: int, value: int) -> None:
        self._write(bytes((0xB0, control, value)))

    def send_sysex(self, data: bytes) -> None:
        self._write(bytes([0xF0]) + data + bytes([0xF7]))

    def _read_available(self) -> None:
        while True:
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                break
            if not chunk:
                break
            for byte in chunk:
                if byte >= 0xF8:  # Real-time messages may occur inside SysEx.
                    continue
                if byte == 0xF0:
                    self._sysex = bytearray()
                elif byte == 0xF7 and self._sysex is not None:
                    self._completed.append(bytes(self._sysex))
                    self._sysex = None
                elif self._sysex is not None and byte < 0x80:
                    self._sysex.append(byte)

    def iter_sysex(self) -> list[bytes]:
        self._read_available()
        messages, self._completed = self._completed, []
        return messages

    def drain(self) -> None:
        self._read_available()
        self._completed.clear()


class PocketMasterSession:
    def __init__(self, device: str, timeout: float = 2.0):
        self.device = device
        self.timeout = timeout
        self.port = RawMidiPort(device)

    def __enter__(self) -> "PocketMasterSession":
        self.port.__enter__()
        time.sleep(0.15)
        self.port.drain()
        return self

    def __exit__(self, *args: object) -> None:
        self.port.__exit__(*args)

    def send_cc(self, control: int, value: int) -> None:
        self.port.send_cc(control, value)

    def _request_single(self, request: bytes, prefix: bytes, length: int) -> bytes:
        self.port.send_sysex(request)
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            for data in self.port.iter_sysex():
                try:
                    _, _, payload = decode_sysex_data(data)
                except ProtocolError:
                    continue
                if payload.startswith(prefix):
                    # A preset change can emit an unsolicited message sharing
                    # the 12 43 prefix but carrying additional fields. Ignore
                    # that notification and wait for the exact read response.
                    if len(payload) == length:
                        return payload
            time.sleep(0.01)
        raise ProtocolError(f"timed out waiting for {prefix.hex(' ')}")

    def _request_multipart(
        self,
        request: bytes,
        command: int,
        parts: int,
        prefix: bytes,
        length: int,
    ) -> bytes:
        collector = FragmentCollector(command, parts, prefix, length)
        self.port.send_sysex(request)
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            for data in self.port.iter_sysex():
                try:
                    frame_command, index, payload = decode_sysex_data(data)
                except ProtocolError:
                    continue
                logical = collector.accept(frame_command, index, payload)
                if logical is not None:
                    return logical
            time.sleep(0.01)
        raise ProtocolError(f"timed out waiting for complete {prefix.hex(' ')} response")

    def read_live_state(self):
        first = decode_current_preset(
            self._request_single(CURRENT_PRESET_REQUEST, b"\x12\x43", 4)
        )
        logical = self._request_multipart(
            LIVE_STATE_REQUEST, 0x1A, 26, b"\x12\x41", 476
        )
        second = decode_current_preset(
            self._request_single(CURRENT_PRESET_REQUEST, b"\x12\x43", 4)
        )
        if first != second:
            raise ProtocolError("preset changed during coordinated state refresh")
        return decode_live_state(logical, first)

    def read_global_settings(self):
        logical = self._request_multipart(
            GLOBAL_SETTINGS_REQUEST, 0x04, 4, b"\x12\x10", 71
        )
        return decode_global_settings(logical)

    def consume_feedback(self, state: ControllerState) -> None:
        for data in self.port.iter_sysex():
            try:
                _, _, logical = decode_sysex_data(data)
            except ProtocolError:
                continue
            feedback = decode_module_feedback(logical)
            if feedback is None:
                continue
            module, enabled = feedback
            controller_name = {"DRV": "DRIVE", "DLY": "DELAY", "RVB": "REVERB"}.get(module)
            if controller_name:
                from .controller import Value

                state.modules[controller_name] = Value(enabled, Certainty.CONFIRMED)


class Gp5Session:
    """Minimal GP-5 USB session for current-preset and effect-state reads."""

    def __init__(self, device: str, timeout: float = 2.0):
        self.timeout = timeout
        self.port = RawMidiPort(device)

    def __enter__(self) -> "Gp5Session":
        self.port.__enter__()
        time.sleep(0.15)
        self.port.drain()
        return self

    def __exit__(self, *args: object) -> None:
        self.port.__exit__(*args)

    def send_cc(self, control: int, value: int) -> None:
        self.port.send_cc(control, value)

    def read_state(self) -> Gp5State:
        self.port.drain()
        self.port.send_sysex(GP5_CURRENT_PRESET_REQUEST)
        deadline = time.monotonic() + self.timeout
        preset: int | None = None
        while time.monotonic() < deadline and preset is None:
            for body in self.port.iter_sysex():
                try:
                    preset = decode_gp5_current_preset(body)
                    break
                except ProtocolError:
                    continue
            time.sleep(0.01)
        if preset is None:
            raise ProtocolError("timed out waiting for GP-5 current preset")

        self.port.drain()
        self.port.send_sysex(GP5_PATCH_STATE_REQUEST)
        deadline = time.monotonic() + self.timeout
        modules: dict[str, bool] | None = None
        while time.monotonic() < deadline and modules is None:
            for body in self.port.iter_sysex():
                modules = decode_patch_state_fragment(body)
                if modules is not None:
                    break
            time.sleep(0.01)
        if modules is None:
            raise ProtocolError("timed out waiting for GP-5 patch state fragment")
        return Gp5State(preset, modules)


def apply_live_state(controller: ControllerState, live: Any) -> None:
    from .controller import Value

    controller.preset = Value(live.preset_index, Certainty.CONFIRMED)
    controller.bank = live.preset_index // 4
    mapping = {"DRIVE": "DRV", "DELAY": "DLY", "REVERB": "RVB"}
    for controller_name, module_name in mapping.items():
        controller.modules[controller_name] = Value(
            live.modules[module_name], Certainty.CONFIRMED
        )


def apply_gp5_state(controller: ControllerState, live: Gp5State) -> None:
    from .controller import Value

    controller.preset = Value(live.preset_index, Certainty.CONFIRMED)
    controller.bank = live.preset_index // 4
    for name, enabled in live.modules.items():
        controller.modules[name] = Value(enabled, Certainty.CONFIRMED)


def print_state(controller: ControllerState, profile: str = "pocket-master") -> None:
    preset = (
        (gp5_preset_label if profile == "gp5" else preset_label)(int(controller.preset.value))
        if controller.preset.value is not None
        else "?"
    )
    effects = " ".join(
        f"{name}={'ON' if value.value else 'off' if value.value is not None else '?'}"
        f"[{value.certainty.value}]"
        for name, value in controller.modules.items()
    )
    tuner = (
        "ON" if controller.tuner.value else "off"
        if controller.tuner.value is not None
        else "?"
    )
    print(
        f"STATE bank={controller.bank + 1:02d} "
        f"preset={preset}[{controller.preset.certainty.value}] {effects} "
        f"TUNER={tuner}[{controller.tuner.certainty.value}]"
    )


def configure_serial(path: str) -> int:
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def list_ports(_: argparse.Namespace) -> None:
    print("ALSA cards:")
    try:
        with open("/proc/asound/cards", encoding="utf-8") as cards:
            print(cards.read().rstrip())
    except OSError as error:
        print(f"  unavailable: {error}")
    print("\nRaw MIDI devices:")
    devices = sorted(glob.glob("/dev/snd/midiC*D*"))
    for path in devices:
        print(f"  {path}")
    if not devices:
        print("  (none)")


def read_state_command(args: argparse.Namespace) -> None:
    if args.device_profile == "gp5":
        with Gp5Session(args.midi_device, args.timeout) as pedal:
            live = pedal.read_state()
        print(f"Preset: {gp5_preset_label(live.preset_index)} ({live.preset_index})")
        print("Modules: " + " ".join(f"{k}={'ON' if v else 'off'}" for k, v in live.modules.items()))
        return
    with PocketMasterSession(args.midi_device, args.timeout) as pedal:
        global_settings = pedal.read_global_settings()
        live = pedal.read_live_state()
    print(f"Firmware: {global_settings.firmware or 'unknown'}")
    print(f"Preset: {preset_label(live.preset_index)} ({live.preset_index})")
    print(f"Tempo: {live.tempo_bpm} BPM; preset volume: {live.preset_volume}")
    print("Modules: " + " ".join(f"{k}={'ON' if v else 'off'}" for k, v in live.modules.items()))
    print("Chain: " + " -> ".join(live.chain))


def run_controller(args: argparse.Namespace) -> None:
    state = ControllerState()
    if args.tuner_starting != "unknown":
        from .controller import Value

        state.tuner = Value(args.tuner_starting == "on", Certainty.ASSUMED)
    serial_fd = configure_serial(args.serial)
    serial_buffer = bytearray()
    try:
        session_type = Gp5Session if args.device_profile == "gp5" else PocketMasterSession
        with session_type(args.midi_device, args.timeout) as pedal:
            if args.device_profile == "gp5":
                apply_gp5_state(state, pedal.read_state())
            else:
                apply_live_state(state, pedal.read_live_state())
            print_state(state, args.device_profile)
            print("Controller running. Ctrl-C stops it; no preset-save commands exist.")

            while True:
                if args.device_profile == "pocket-master":
                    pedal.consume_feedback(state)
                readable, _, _ = select.select([serial_fd], [], [], 0.02)
                if not readable:
                    continue
                chunk = os.read(serial_fd, 4096)
                if not chunk:
                    continue
                serial_buffer.extend(chunk)
                while b"\n" in serial_buffer:
                    raw, _, remainder = serial_buffer.partition(b"\n")
                    serial_buffer[:] = remainder
                    event = parse_switch_event(raw.decode("utf-8", "replace").strip("\r"))
                    if event is None or not event.pressed:
                        continue
                    handler = handle_gp5_press if args.device_profile == "gp5" else handle_press
                    command = handler(state, event.name)
                    if command is None:
                        print_state(state, args.device_profile)
                        continue
                    pedal.send_cc(command.control, command.value)
                    print(f"SENT CC{command.control}={command.value}: {command.description}")
                    time.sleep(0.25)
                    try:
                        if command.refresh == "live":
                            if args.device_profile == "gp5":
                                apply_gp5_state(state, pedal.read_state())
                            else:
                                apply_live_state(state, pedal.read_live_state())
                    except ProtocolError as error:
                        print(f"State remains assumed; confirmation failed: {error}")
                    print_state(state, args.device_profile)
    finally:
        os.close(serial_fd)


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description=__doc__)
    sub = root.add_subparsers(dest="command", required=True)
    ports = sub.add_parser("ports", help="list exact USB-MIDI port names")
    ports.set_defaults(func=list_ports)

    read = sub.add_parser("read-state", help="perform bounded read-only state refresh")
    read.add_argument("--midi-device", default="/dev/snd/midiC2D0")
    read.add_argument("--device-profile", choices=("pocket-master", "gp5"), default="pocket-master")
    read.add_argument("--timeout", type=float, default=2.0)
    read.set_defaults(func=read_state_command)

    run = sub.add_parser("run", help="bridge ESP32 footswitch serial to pedal USB")
    run.add_argument("--serial", default="/dev/ttyACM0")
    run.add_argument("--midi-device", default="/dev/snd/midiC2D0")
    run.add_argument("--device-profile", choices=("pocket-master", "gp5"), default="pocket-master")
    run.add_argument("--timeout", type=float, default=2.0)
    run.add_argument(
        "--tuner-starting",
        choices=("unknown", "on", "off"),
        default="unknown",
        help="visible tuner state at startup; tracked locally because USB readback is unreliable",
    )
    run.set_defaults(func=run_controller)
    return root


def main() -> None:
    args = parser().parse_args()
    try:
        args.func(args)
    except KeyboardInterrupt:
        print("\nController stopped.")
    except (ProtocolError, OSError) as error:
        raise SystemExit(f"error: {error}") from error


if __name__ == "__main__":
    main()
