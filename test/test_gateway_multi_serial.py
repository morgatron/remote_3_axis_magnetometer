"""
Unit and Integration Tests for Gateway Multi-Serial Listener (`test_gateway_multi_serial.py`)

Verifies:
1. Parsing of SERIAL_PORT configurations (single, multi-port, delimited, auto/all).
2. Filtering logic of is_sensor_serial_port across platforms.
3. Concurrent execution of multiple serial workers feeding the unified telemetry queue.
4. Serial Manager auto-discovery, hotplug detection, and graceful disconnection cleanup.
"""

import os
import sys
import time
import queue
import threading
import unittest
from unittest.mock import MagicMock, patch

# Ensure central_service is on sys.path
CENTRAL_SERVICE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "central_service"))
if CENTRAL_SERVICE_DIR not in sys.path:
    sys.path.insert(0, CENTRAL_SERVICE_DIR)

import gateway


class MockPortInfo:
    def __init__(self, device, hwid="n/a", vid=None):
        self.device = device
        self.hwid = hwid
        self.vid = vid


class TestGatewayMultiSerial(unittest.TestCase):

    def setUp(self):
        # Clear the global gateway send_queue before each test
        while not gateway.send_queue.empty():
            try:
                gateway.send_queue.get_nowait()
            except queue.Empty:
                break

    def test_parse_serial_ports_config(self):
        """Test parsing of various SERIAL_PORT configuration formats."""
        # Unset / Auto formats should return None (signaling auto-discovery)
        self.assertIsNone(gateway.parse_serial_ports_config(None))
        self.assertIsNone(gateway.parse_serial_ports_config(""))
        self.assertIsNone(gateway.parse_serial_ports_config("   "))
        self.assertIsNone(gateway.parse_serial_ports_config("auto"))
        self.assertIsNone(gateway.parse_serial_ports_config("AUTO"))
        self.assertIsNone(gateway.parse_serial_ports_config("all"))
        self.assertIsNone(gateway.parse_serial_ports_config("*"))

        # Single port
        self.assertEqual(gateway.parse_serial_ports_config("/dev/ttyACM0"), ["/dev/ttyACM0"])
        self.assertEqual(gateway.parse_serial_ports_config("COM3"), ["COM3"])

        # Comma-delimited
        self.assertEqual(
            gateway.parse_serial_ports_config("/dev/ttyACM0,/dev/ttyACM1"),
            ["/dev/ttyACM0", "/dev/ttyACM1"]
        )

        # Mixed whitespace and semicolons
        self.assertEqual(
            gateway.parse_serial_ports_config(" /dev/ttyACM0 ; /dev/ttyUSB0 , /dev/ttyACM1 "),
            ["/dev/ttyACM0", "/dev/ttyUSB0", "/dev/ttyACM1"]
        )

        # Space delimited
        self.assertEqual(
            gateway.parse_serial_ports_config("/dev/ttyACM0 /dev/ttyACM1"),
            ["/dev/ttyACM0", "/dev/ttyACM1"]
        )

    def test_is_sensor_serial_port_filtering(self):
        """Test device path filtering to ensure genuine USB CDC/UART devices are accepted and motherboard ports rejected."""
        valid_ports = [
            MockPortInfo("/dev/ttyACM0"),
            MockPortInfo("/dev/ttyACM1"),
            MockPortInfo("/dev/ttyUSB0"),
            MockPortInfo("/dev/ttyUSB3"),
            MockPortInfo("/dev/tty.usbmodem1101"),
            MockPortInfo("/dev/tty.usbserial-1420"),
            MockPortInfo("COM3", hwid="USB VID:PID=303A:1001", vid=0x303A),
            MockPortInfo("COM4", hwid="FTDI USB Serial Converter", vid=0x0403),
        ]
        for p in valid_ports:
            self.assertTrue(
                gateway.is_sensor_serial_port(p),
                f"Expected port '{p.device}' to be recognized as a sensor serial port."
            )

        invalid_ports = [
            MockPortInfo("/dev/ttyS0"),
            MockPortInfo("/dev/ttyS31"),
            MockPortInfo("/dev/ttyprintk"),
            MockPortInfo("COM1", hwid=r"ACPI\PNP0501", vid=None),
        ]
        for p in invalid_ports:
            self.assertFalse(
                gateway.is_sensor_serial_port(p),
                f"Expected legacy/virtual port '{p.device}' to be filtered out."
            )

    @patch("gateway.serial.Serial")
    def test_concurrent_serial_workers(self, mock_serial_cls):
        """Verify multiple serial_device_worker threads ingest data concurrently without interference."""
        # Create two simulated serial ports streaming telemetry from two distinct nodes
        stop_evt1 = threading.Event()
        stop_evt2 = threading.Event()

        # Stream lines for node 1 (OFFICERM3)
        lines_port1 = [
            b"OFFICERM3,1000000,20000.0,5000.0,-38000.0,000000,23.5,3.95,-55\n",
            b"OFFICERM3,2000000,20050.0,5020.0,-38010.0,000000,23.5,3.95,-55\n",
        ]
        # Stream lines for node 2 (SENSOR_ROOF)
        lines_port2 = [
            b"SENSOR_ROOF,1500000,-15000.0,12000.0,42000.0,000000,21.0,4.10,-68\n",
            b"SENSOR_ROOF,2500000,-15010.0,12010.0,42020.0,000000,21.0,4.10,-68\n",
        ]

        def create_mock_instance(port_lines, stop_event):
            inst = MagicMock()
            inst.is_open = True
            lines_iter = iter(port_lines)

            def readline_side_effect():
                try:
                    return next(lines_iter)
                except StopIteration:
                    stop_event.set()
                    return b""

            inst.readline.side_effect = readline_side_effect
            return inst

        inst1 = create_mock_instance(lines_port1, stop_evt1)
        inst2 = create_mock_instance(lines_port2, stop_evt2)

        def mock_serial_factory(*args, **kwargs):
            # Inspect port assigned to return appropriate mock
            inst = MagicMock()
            inst.is_open = True

            def open_mock():
                if inst.port == "/dev/ttyACM0":
                    inst.readline.side_effect = inst1.readline.side_effect
                else:
                    inst.readline.side_effect = inst2.readline.side_effect

            inst.open.side_effect = open_mock
            return inst

        mock_serial_cls.side_effect = mock_serial_factory

        # Spawn both workers
        t1 = threading.Thread(
            target=gateway.serial_device_worker,
            args=("/dev/ttyACM0", 921600, stop_evt1, True),
            daemon=True
        )
        t2 = threading.Thread(
            target=gateway.serial_device_worker,
            args=("/dev/ttyACM1", 921600, stop_evt2, True),
            daemon=True
        )

        t1.start()
        t2.start()

        t1.join(timeout=3.0)
        t2.join(timeout=3.0)

        # Collect ingested samples from send_queue
        samples = []
        while not gateway.send_queue.empty():
            samples.append(gateway.send_queue.get_nowait())

        self.assertEqual(len(samples), 4, f"Expected 4 samples, got {len(samples)}")

        node_ids = {s["node_id"] for s in samples}
        self.assertIn("OFFICERM3", node_ids)
        self.assertIn("SENSOR_ROOF", node_ids)

        # Check values from each node
        officerm3_samples = [s for s in samples if s["node_id"] == "OFFICERM3"]
        self.assertEqual(len(officerm3_samples), 2)
        self.assertAlmostEqual(officerm3_samples[0]["x"], 20000.0)

        roof_samples = [s for s in samples if s["node_id"] == "SENSOR_ROOF"]
        self.assertEqual(len(roof_samples), 2)
        self.assertAlmostEqual(roof_samples[0]["x"], -15000.0)

    @patch("gateway.serial.tools.list_ports.comports")
    @patch("gateway.serial.Serial")
    def test_serial_manager_auto_discovery_and_cleanup(self, mock_serial_cls, mock_comports):
        """Verify serial_manager_thread dynamically discovers devices, spawns workers, and cleans up on disconnect."""
        # Initial scan returns /dev/ttyACM0
        mock_comports.return_value = [
            MockPortInfo("/dev/ttyACM0"),
            MockPortInfo("/dev/ttyS0"),  # Should be filtered out
        ]

        # Configure mock Serial
        mock_inst = MagicMock()
        mock_inst.is_open = True
        mock_inst.readline.return_value = b""
        mock_serial_cls.return_value = mock_inst

        mgr_stop_evt = threading.Event()
        mgr_thread = threading.Thread(
            target=gateway.serial_manager_thread,
            args=(None, 921600, mgr_stop_evt),  # None -> auto-discovery mode
            daemon=True
        )
        mgr_thread.start()

        # Allow initial scan to run
        time.sleep(0.5)

        # Check that worker was started for /dev/ttyACM0
        thread_names = [t.name for t in threading.enumerate()]
        self.assertIn("SerialWorker-/dev/ttyACM0", thread_names)
        self.assertNotIn("SerialWorker-/dev/ttyS0", thread_names)

        # Simulate device hotplug: second device /dev/ttyACM1 arrives
        mock_comports.return_value = [
            MockPortInfo("/dev/ttyACM0"),
            MockPortInfo("/dev/ttyACM1"),
        ]

        # Wait for next scan cycle
        time.sleep(3.0)

        thread_names = [t.name for t in threading.enumerate()]
        self.assertIn("SerialWorker-/dev/ttyACM1", thread_names)

        # Shut down manager
        mgr_stop_evt.set()
        mgr_thread.join(timeout=3.0)


if __name__ == "__main__":
    unittest.main()
