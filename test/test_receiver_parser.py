"""
Unit Test for ESP32 Receiver Output Compatibility (`test_receiver_parser.py`)

Verifies that the CSV lines produced by the ESP32 Receiver Node conform to
the repository's stream_parser specifications and central_service gateway requirements,
including NodeEpochTracker for accurate historical backlog timestamping.
"""

import os
import sys
import unittest

# Add central_service directory to sys.path
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "central_service")))
from stream_parser import parse_telemetry_line, parse_telemetry_batch, NodeEpochTracker

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
        self.assertAlmostEqual(parsed["vbat"], 3.75)
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

    def test_epoch_tracker_backlog_recovery(self):
        """Test that NodeEpochTracker preserves exact historical UTC timestamps during backlog catch-up."""
        tracker = NodeEpochTracker()
        node_id = "BENCH"

        # Baseline: live stream sample at T_wall = 1700000000.0 with node uptime 100.0s (100,000,000 us)
        t_base_wall = 1700000000.0
        ts_base_us = 100_000_000.0
        iso_base = tracker.get_sample_iso(node_id, ts_base_us, arrival_wall_time=t_base_wall)
        self.assertEqual(iso_base, "2023-11-14T22:13:20Z")

        # Now simulate 30 seconds of outage:
        # During the outage, the node sampled at uptime 110s, 120s, 130s.
        # But arrival happens at T_wall = 1700000030.0 (30 seconds later).
        t_catchup_wall = 1700000030.0

        # Sample taken at uptime 110s (was generated 20 seconds ago at T_wall = 1700000010.0)
        iso_backlog_1 = tracker.get_sample_iso(node_id, 110_000_000.0, arrival_wall_time=t_catchup_wall)
        # Sample taken at uptime 120s (was generated 10 seconds ago at T_wall = 1700000020.0)
        iso_backlog_2 = tracker.get_sample_iso(node_id, 120_000_000.0, arrival_wall_time=t_catchup_wall)
        # Sample taken at uptime 130s (live sample right now at T_wall = 1700000030.0)
        iso_live = tracker.get_sample_iso(node_id, 130_000_000.0, arrival_wall_time=t_catchup_wall)

        # Verify historical timestamps are correctly placed in the past!
        self.assertEqual(iso_backlog_1, "2023-11-14T22:13:30Z") # +10s from baseline
        self.assertEqual(iso_backlog_2, "2023-11-14T22:13:40Z") # +20s from baseline
        self.assertEqual(iso_live, "2023-11-14T22:13:50Z")      # +30s from baseline

    def test_epoch_tracker_reboot_detection(self):
        """Test that NodeEpochTracker handles node reboots (timestamp reset to near 0)."""
        tracker = NodeEpochTracker()
        node_id = "BENCH"

        # Pre-reboot: uptime 500s at T_wall = 1700000500.0
        iso_before = tracker.get_sample_iso(node_id, 500_000_000.0, arrival_wall_time=1700000500.0)
        self.assertEqual(iso_before, "2023-11-14T22:21:40Z")

        # Node reboots: uptime jumps back to 2.0s at T_wall = 1700000510.0
        iso_after_reboot = tracker.get_sample_iso(node_id, 2_000_000.0, arrival_wall_time=1700000510.0)
        self.assertEqual(iso_after_reboot, "2023-11-14T22:21:50Z")

if __name__ == "__main__":
    unittest.main()
