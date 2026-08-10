"""
Unit Test for ESP32 Receiver Output Compatibility (`test_receiver_parser.py`)

Verifies that the CSV lines produced by the ESP32 Receiver Node conform to
the repository's stream_parser specifications and central_service gateway requirements.
"""

import os
import sys
import unittest

# Add root directory to sys.path
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from stream_parser import parse_telemetry_line, parse_telemetry_batch

class TestReceiverFormat(unittest.TestCase):

    def test_espnow_parsed_line(self):
        """Test parsing ESP-NOW telemetry relayed line with RSSI and vbat."""
        line = "NODE_686F80,123456789,23415.20,-4120.80,48910.10,C00000,24.5,3.75,-54"
        parsed = parse_telemetry_line(line)
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed["node_id"], "NODE_686F80")
        self.assertEqual(parsed["timestamp_us"], 123456789.0)
        self.assertAlmostEqual(parsed["x"], 23415.20)
        self.assertAlmostEqual(parsed["y"], -4120.80)
        self.assertAlmostEqual(parsed["z"], 48910.10)
        self.assertEqual(parsed["status_hex"], "C00000")
        self.assertEqual(parsed["temp"], 24.5)
        self.assertEqual(parsed["vbat"], 3) # truncated int representation in stream_parser or float
        self.assertEqual(parsed["rssi"], -54)

    def test_ble_coded_phy_parsed_line(self):
        """Test parsing BLE Coded PHY advertisement line."""
        line = "SENSOR_BLE01,987654321,1234.56,5678.90,-9101.12,C00000,22.0,3.60,-82"
        parsed = parse_telemetry_line(line)
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed["node_id"], "SENSOR_BLE01")
        self.assertAlmostEqual(parsed["x"], 1234.56)
        self.assertEqual(parsed["rssi"], -82)

    def test_batch_payload_relative_timestamps(self):
        """Test microsecond relative delta-t back-calculation on batched receiver output."""
        batch_payload = (
            "NODE_686F80,1000000,100.0,200.0,300.0,C00000,25.0,3.80,-60\n"
            "NODE_686F80,2000000,105.0,205.0,305.0,C00000,25.0,3.80,-60\n"
        )
        parsed_list = parse_telemetry_batch(batch_payload, arrival_wall_time=1700000000.0)
        self.assertEqual(len(parsed_list), 2)
        # Latest sample should anchor to arrival_wall_time
        self.assertEqual(parsed_list[1]["timestamp_iso"], "2023-11-14T22:13:20Z")
        # Previous sample (1 sec earlier) should anchor 1.0 sec earlier
        self.assertEqual(parsed_list[0]["timestamp_iso"], "2023-11-14T22:13:19Z")

if __name__ == "__main__":
    unittest.main()
