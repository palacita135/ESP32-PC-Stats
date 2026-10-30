import json
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import pc_monitor


class PcMonitorTests(unittest.TestCase):
    def test_encode_sample_matches_firmware_protocol(self):
        sample = pc_monitor.Metrics(
            cpu=12.5,
            mem=40.0,
            mem_used=6.0,
            mem_total=16.0,
            gpu=None,
            gpu_temp=None,
            gpu_mem=None,
            gpu_mem_total=None,
            down=1024.0,
            up=256.0,
            uptime=3,
        )

        encoded = pc_monitor.encode_sample(sample)

        self.assertTrue(encoded.endswith(b"\n"))
        decoded = json.loads(encoded)
        self.assertEqual(decoded["cpu"], 12.5)
        self.assertIsNone(decoded["gpu"])
        self.assertEqual(decoded["uptime"], 3)

    @patch("pc_monitor.list_ports.comports")
    def test_find_serial_port_selects_ch340(self, comports):
        comports.return_value = [
            SimpleNamespace(device="COM1", vid=0x1234, pid=0x5678),
            SimpleNamespace(device="COM5", vid=pc_monitor.CH340_VENDOR_ID,
                            pid=pc_monitor.CH340_PRODUCT_ID),
        ]

        self.assertEqual(pc_monitor.find_serial_port(), "COM5")

    @patch("pc_monitor.list_ports.comports")
    def test_find_serial_port_reports_missing_ch340(self, comports):
        comports.return_value = [SimpleNamespace(device="COM1", vid=0x1234, pid=0x5678)]

        with self.assertRaisesRegex(RuntimeError, "No CH340 adapter found"):
            pc_monitor.find_serial_port()


if __name__ == "__main__":
    unittest.main()
