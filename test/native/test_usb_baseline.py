"""Source-level guard for settings and recovery validated during USB bring-up.

These assertions record the committed baseline; they do not assert hardware
reliability or prove the GP-5 response mapping is correct.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class UsbBaselineTests(unittest.TestCase):
    def test_hub_timing_and_capacity(self):
        config = (ROOT / "sdkconfig.defaults").read_text()
        expected = {
            "CONFIG_USB_HOST_HUBS_SUPPORTED": "y",
            "CONFIG_USB_HOST_HUB_MULTI_LEVEL": "y",
            "CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE": "512",
            "CONFIG_USB_HOST_EXT_PORT_CUSTOM_POWER_ON_DELAY_MS": "250",
            "CONFIG_USB_HOST_EXT_PORT_RESET_RECOVERY_DELAY_MS": "100",
            "CONFIG_USB_HOST_DEBOUNCE_DELAY_MS": "250",
            "CONFIG_USB_HOST_RESET_HOLD_MS": "50",
            "CONFIG_USB_HOST_RESET_RECOVERY_MS": "100",
            "CONFIG_USB_HOST_SET_ADDR_RECOVERY_MS": "20",
        }
        for key, value in expected.items():
            with self.subTest(key=key):
                self.assertIn(f"{key}={value}", config)

    def test_endpoint_recovery_and_receive_bounds(self):
        transport = (ROOT / "src/usb_midi_transport.cpp").read_text()
        header = (ROOT / "include/usb_midi_transport.h").read_text()
        self.assertIn("kUsbReceiveTransferSize = 4096", header)
        self.assertIn("kReceiveBufferCapacity = 2048", header)
        self.assertIn("usb_host_endpoint_flush(device_, midiEndpointIn_)", transport)
        self.assertIn("usb_host_endpoint_clear(device_, midiEndpointIn_)", transport)
        self.assertIn("receivingSysEx_ = false;", transport)
        self.assertIn("receiveLength_ = 0;", transport)

    def test_gp5_sync_timing_is_recorded(self):
        plugin = (ROOT / "src/gp5_plugin.cpp").read_text()
        self.assertIn("syncStartedAtMs_ >= 5000", plugin)
        self.assertIn("stateResponseStartedMs_ >= 1000", plugin)
        self.assertIn("schedule(Request::PatchNames, now, 100)", plugin)
        self.assertIn("scheduleStateRequest(now, 350)", plugin)
        self.assertIn("scheduleStateRequest(now, 150)", plugin)


if __name__ == "__main__":
    unittest.main()
