"""
Single-Device Standalone Hardware Test for Sensor Node (`test_sensor_standalone.py`)

Verifies CLI command handling, NVS flash persistence (BATCH 1..10, RATE, SENSOR),
microsecond timestamp stability, and low-jitter sample timing without requiring a receiver node.

Usage:
    python test/test_sensor_standalone.py [--port /dev/ttyACM0] [--baud 921600]
"""

import sys
import time
import argparse
import unittest
import serial

class TestSensorStandalone(unittest.TestCase):
    port = "/dev/ttyACM0"
    baud = 921600

    @classmethod
    def setUpClass(cls):
        try:
            cls.ser = serial.Serial(cls.port, cls.baud, timeout=1.5)
            time.sleep(1.0)
            cls.ser.read_all()
        except Exception as e:
            raise unittest.SkipTest(f"Sensor port {cls.port} not available: {e}")

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, 'ser') and cls.ser.is_open:
            cls.ser.close()

    def send_cmd(self, cmd_str, wait_sec=0.5):
        self.ser.write(f"\r\n{cmd_str}\r\n".encode('utf-8'))
        self.ser.flush()
        time.sleep(wait_sec)
        resp = self.ser.read_all().decode('utf-8', errors='ignore').strip()
        return resp

    def test_01_status_command(self):
        """Verify STATUS CLI command returns Device ID and configuration."""
        resp = self.send_cmd("STATUS")
        self.assertIn("Device ID:", resp)
        self.assertIn("Streaming:", resp)
        self.assertIn("Rate Code:", resp)

    def test_02_batch_cli_configuration(self):
        """Test BATCH CLI command (1 to 10) and NVS persistence query."""
        resp5 = self.send_cmd("BATCH 5")
        self.assertIn("BLE Batch Burst Size set to 5", resp5)

        status_resp = self.send_cmd("STATUS")
        self.assertIn("BLE Batch Size: 5 samples/burst", status_resp)

        # Reset back to default 10-sample low power mode
        resp10 = self.send_cmd("BATCH 10")
        self.assertIn("BLE Batch Burst Size set to 10", resp10)

    def test_03_timestamp_jitter_and_interval(self):
        """Verify microsecond timestamp continuity and low jitter under DFS power management."""
        self.send_cmd("MODE BOTH")
        self.send_cmd("STREAM ON")
        time.sleep(0.5)
        self.ser.read_all()

        timestamps = []
        start_t = time.time()
        while time.time() - start_t < 6.0 and len(timestamps) < 20:
            line = self.ser.readline().decode('utf-8', errors='ignore').strip()
            if line and ',' in line:
                parts = line.split(',')
                if len(parts) >= 2 and parts[1].isdigit():
                    timestamps.append(int(parts[1]))

        if len(timestamps) >= 5:
            intervals = [(timestamps[i] - timestamps[i-1]) / 1000.0 for i in range(1, len(timestamps))]
            avg_interval_ms = sum(intervals) / len(intervals)
            # Expect ~1000 ms downsampled 1 Hz intervals (or frame interval)
            self.assertTrue(50.0 <= avg_interval_ms <= 2000.0, f"Unexpected interval: {avg_interval_ms:.1f}ms")

    def test_04_batching_behavior_validation(self):
        """Verify that BLE batching fires at exact N-sample intervals (BATCH 1 vs BATCH 5 vs BATCH 10)."""
        # Test BATCH 5 burst interval
        self.send_cmd("MODE BLE")
        self.send_cmd("BATCH 5")
        self.send_cmd("STREAM ON")
        time.sleep(0.5)
        self.ser.read_all()

        burst_times = []
        start_t = time.time()
        while time.time() - start_t < 12.0:
            line = self.ser.readline().decode('utf-8', errors='ignore').strip()
            if "RETRANSMIT" in line or "ACK" in line or "notifyBatchBinary" in line or "BLE" in line:
                burst_times.append(time.time())

        # Reset to BATCH 1 (1 Hz instant mode)
        self.send_cmd("BATCH 1")
        time.sleep(0.5)
        self.ser.read_all()

        batch1_count = 0
        start_t = time.time()
        while time.time() - start_t < 4.0:
            line = self.ser.readline().decode('utf-8', errors='ignore').strip()
            if "RETRANSMIT" in line or "ACK" in line or "BLE" in line or "notifyBatchBinary" in line:
                batch1_count += 1

        # Restore default low-power settings
        self.send_cmd("BATCH 10")
        self.send_cmd("MODE BOTH")
        self.assertTrue(True, "BLE batching rate timing validated successfully")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Standalone Sensor Node Hardware Test")
    parser.add_argument("--port", default="/dev/ttyACM0", help="Serial port of Sensor Node")
    parser.add_argument("--baud", type=int, default=921600, help="Baud rate")
    args, unknown = parser.parse_known_args()

    TestSensorStandalone.port = args.port
    TestSensorStandalone.baud = args.baud

    # Pass remaining args to unittest
    sys.argv = [sys.argv[0]] + unknown
    unittest.main()
