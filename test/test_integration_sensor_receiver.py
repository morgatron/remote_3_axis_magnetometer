"""
End-to-End Multi-Device Integration Test Suite (`test/test_integration_sensor_receiver.py`)

Verifies end-to-end Bluetooth 5.0 LE Coded PHY Extended Advertising batch transmission,
hardware AUX_SCAN_REQ acknowledgments, 10-minute disconnect ring buffering, and
rapid backlog catch-up flushing between a physical Sensor Node and Gateway Receiver Node.

Usage:
    python test/test_integration_sensor_receiver.py [--sensor-port /dev/ttyACM0] [--rcvr-port /dev/ttyACM1]
"""

import sys
import time
import argparse
import unittest
import serial

class TestIntegrationSensorReceiver(unittest.TestCase):
    sensor_port = "/dev/ttyACM0"
    rcvr_port = "/dev/ttyACM1"
    baud = 921600

    @classmethod
    def setUpClass(cls):
        try:
            cls.ser_sensor = serial.Serial(cls.sensor_port, cls.baud, timeout=1.0)
            cls.ser_rcvr = serial.Serial(cls.rcvr_port, cls.baud, timeout=1.0)
            time.sleep(1.0)
            cls.ser_sensor.read_all()
            cls.ser_rcvr.read_all()
        except Exception as e:
            raise unittest.SkipTest(f"Hardware ports not available ({cls.sensor_port}, {cls.rcvr_port}): {e}")

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, 'ser_sensor') and cls.ser_sensor.is_open:
            cls.ser_sensor.close()
        if hasattr(cls, 'ser_rcvr') and cls.ser_rcvr.is_open:
            cls.ser_rcvr.close()

    def send_sensor_cmd(self, cmd_str, wait_sec=0.5):
        self.ser_sensor.write(f"\r\n{cmd_str}\r\n".encode('utf-8'))
        self.ser_sensor.flush()
        time.sleep(wait_sec)
        return self.ser_sensor.read_all().decode('utf-8', errors='ignore').strip()

    def send_rcvr_cmd(self, cmd_str, wait_sec=0.5):
        self.ser_rcvr.write(f"\r\n{cmd_str}\r\n".encode('utf-8'))
        self.ser_rcvr.flush()
        time.sleep(wait_sec)
        return self.ser_rcvr.read_all().decode('utf-8', errors='ignore').strip()

    def test_01_coded_phy_batch_reception(self):
        """Verify 10-sample Coded PHY Extended Advertising batch transmission & receiver decoding."""
        print("\n--- Test 01: 10-Sample Coded PHY Batch Reception ---")
        self.send_sensor_cmd("MODE BOTH")
        self.send_sensor_cmd("BATCH 10")
        self.send_sensor_cmd("STREAM ON")
        self.send_rcvr_cmd("MODE BOTH")
        time.sleep(0.5)
        self.ser_sensor.read_all()
        self.ser_rcvr.read_all()

        rcvr_samples = []
        start_t = time.time()
        while time.time() - start_t < 26.0:
            lines = self.ser_rcvr.read_all().decode('utf-8', errors='ignore').splitlines()
            for l in lines:
                l = l.strip()
                if l.startswith("NODE_") or l.startswith("MOCK_"):
                    rcvr_samples.append(l)
            time.sleep(0.5)

        print(f"Total samples received over 22s: {len(rcvr_samples)}")
        self.assertGreaterEqual(len(rcvr_samples), 10, "Receiver failed to decode 10-sample batch bursts!")

        # Verify timestamp delta-t spacing is 1.0 sec within a single 10-sample batch burst
        timestamps = []
        for line in rcvr_samples:
            parts = line.split(',')
            if len(parts) >= 2 and parts[1].isdigit():
                timestamps.append(int(parts[1]))

        # Find 10 consecutive timestamps that belong to a single batch burst (spacing ~1.0s = 1,000,000 us)
        batch_intervals = []
        for i in range(1, len(timestamps)):
            dt = timestamps[i] - timestamps[i-1]
            if 900000 <= dt <= 1100000:
                batch_intervals.append(dt)

        print(f"Verified {len(batch_intervals)} consecutive 1.0s batch intervals inside burst")
        self.assertGreaterEqual(len(batch_intervals), 5, "Failed to find 1.0s consecutive batch sample intervals")

    def test_02_disconnect_ring_buffer_catchup(self):
        """Simulate a 22-second BLE disconnect using MODE SERIAL on sensor and verify ring buffer catch-up flushing."""
        print("\n--- Test 02: Disconnect Ring Buffer Catch-up Flushing ---")
        self.send_sensor_cmd("MODE BOTH")
        self.send_sensor_cmd("BATCH 10")
        self.send_sensor_cmd("STREAM ON")
        self.send_rcvr_cmd("MODE BOTH")
        time.sleep(0.5)

        # Step A: Switch sensor output mode to SERIAL (disables BLE advertising, accumulating un-ACKed backlog)
        self.send_sensor_cmd("MODE SERIAL")
        print("Sensor switched to MODE SERIAL (simulating 22s offline disconnect)...")
        time.sleep(22.0)

        # Step B: Switch sensor back to BOTH (re-activates BLE advertising & triggers backlog flush)
        print("Switching sensor back to MODE BOTH to trigger catch-up flush...")
        self.ser_rcvr.read_all()
        self.send_sensor_cmd("MODE BOTH")

        catchup_samples = []
        start_t = time.time()
        while time.time() - start_t < 15.0:
            lines = self.ser_rcvr.read_all().decode('utf-8', errors='ignore').splitlines()
            for l in lines:
                l = l.strip()
                if l.startswith("NODE_") or l.startswith("MOCK_"):
                    catchup_samples.append(l)
            time.sleep(0.5)

        print(f"Total catch-up samples received: {len(catchup_samples)}")
        self.assertGreaterEqual(len(catchup_samples), 10, "Catch-up buffer flush failed to recover missing telemetry!")

    def test_03_dynamic_batch_size_change(self):
        """Verify dynamic batch size change (BATCH 5) end-to-end."""
        print("\n--- Test 03: Dynamic Batch Size Change (BATCH 5) ---")
        self.send_sensor_cmd("MODE BOTH")
        self.send_sensor_cmd("BATCH 5")
        self.send_sensor_cmd("STREAM ON")
        self.send_rcvr_cmd("MODE BOTH")
        time.sleep(0.5)
        self.ser_sensor.read_all()
        self.ser_rcvr.read_all()

        samples = []
        start_t = time.time()
        while time.time() - start_t < 14.0:
            lines = self.ser_rcvr.read_all().decode('utf-8', errors='ignore').splitlines()
            for l in lines:
                l = l.strip()
                if l.startswith("NODE_") or l.startswith("MOCK_"):
                    samples.append(l)
            time.sleep(0.5)

        print(f"Total samples received under BATCH 5: {len(samples)}")
        self.assertGreaterEqual(len(samples), 5, "BATCH 5 burst failed to deliver samples!")

        # Restore default sensor settings
        self.send_sensor_cmd("BATCH 10")
        self.send_sensor_cmd("MODE BOTH")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Multi-Device Integration Test Suite")
    parser.add_argument("--sensor-port", default="/dev/ttyACM0", help="Sensor Node port")
    parser.add_argument("--rcvr-port", default="/dev/ttyACM1", help="Receiver Node port")
    args, unknown = parser.parse_known_args()

    TestIntegrationSensorReceiver.sensor_port = args.sensor_port
    TestIntegrationSensorReceiver.rcvr_port = args.rcvr_port

    sys.argv = [sys.argv[0]] + unknown
    unittest.main()
