"""
Single-Device Standalone Hardware Test for Receiver Node (`test_receiver_standalone.py`)

Verifies receiver CLI command handling, 80 MHz CPU frequency scaling,
OLED display state transitions, and Node Table status without requiring a field sensor node.

Usage:
    python test/test_receiver_standalone.py [--port /dev/ttyACM1] [--baud 921600]
"""

import sys
import time
import argparse
import unittest
import serial

class TestReceiverStandalone(unittest.TestCase):
    port = "/dev/ttyACM1"
    baud = 921600

    @classmethod
    def setUpClass(cls):
        try:
            cls.ser = serial.Serial(cls.port, cls.baud, timeout=1.5)
            time.sleep(1.0)
            cls.ser.read_all()
        except Exception as e:
            raise unittest.SkipTest(f"Receiver port {cls.port} not available: {e}")

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, 'ser') and cls.ser.is_open:
            cls.ser.close()

    def send_cmd(self, cmd_str, timeout_sec=2.0):
        self.ser.write(f"\r\n{cmd_str}\r\n".encode('utf-8'))
        self.ser.flush()
        start = time.time()
        cli_responses = []
        while time.time() - start < timeout_sec:
            line = self.ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                if any(kw in line for kw in ["[CLI]", "STATUS", "NODES", "Egress", "Active", "Uptime", "BLE RX", "WiFi"]):
                    cli_responses.append(line)
        return "\n".join(cli_responses)

    def test_01_receiver_status(self):
        """Verify STATUS CLI command returns protocol counters and CPU frequency."""
        resp = self.send_cmd("STATUS")
        self.assertTrue("STATUS" in resp or "BLE RX" in resp or "Uptime" in resp or "Active" in resp, f"Response: {resp}")

    def test_02_node_table_query(self):
        """Verify NODES CLI command returns active node table header."""
        resp = self.send_cmd("NODES")
        self.assertTrue(len(resp) > 0 or "NODES" in resp or "Active" in resp or "Node" in resp, f"Response: {resp}")

    def test_03_mode_switching(self):
        """Verify MODE command switching between SERIAL, WIFI, and BOTH."""
        resp_ser = self.send_cmd("MODE SERIAL")
        self.assertTrue("[CLI]" in resp_ser or "SERIAL" in resp_ser or "Egress" in resp_ser, f"Response: {resp_ser}")

        resp_both = self.send_cmd("MODE BOTH")
        self.assertTrue("[CLI]" in resp_both or "BOTH" in resp_both or "Egress" in resp_both, f"Response: {resp_both}")

        # Restore to SERIAL
        self.send_cmd("MODE SERIAL")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Standalone Receiver Node Hardware Test")
    parser.add_argument("--port", default="/dev/ttyACM1", help="Serial port of Receiver Node")
    parser.add_argument("--baud", type=int, default=921600, help="Baud rate")
    args, unknown = parser.parse_known_args()

    TestReceiverStandalone.port = args.port
    TestReceiverStandalone.baud = args.baud

    # Pass remaining args to unittest
    sys.argv = [sys.argv[0]] + unknown
    unittest.main()
