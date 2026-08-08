import os
import sys
import math
import random
import time
import serial
import serial.tools.list_ports
from PySide6.QtCore import QThread, Signal

# Import shared stream parser from repository root
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from stream_parser import parse_telemetry_line

class SerialWorker(QThread):
    data_received = Signal(str, object, object, object, object, object)  # device_id, timestamp, x, y, z, status
    status_message = Signal(str)
    connection_status = Signal(bool)

    def __init__(self, port, baudrate=921600):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.running = False
        self.serial_port = None
        self.streaming = True # For mock mode

    def run(self):
        if self.port == "MOCK_SENSOR":
            self.run_mock()
            return

        try:
            self.serial_port = serial.Serial(self.port, self.baudrate, timeout=0.01)
            self.running = True
            self.connection_status.emit(True)
            self.status_message.emit(f"Connected to {self.port}")

            buffer = ""
            while self.running:
                try:
                    data = self.serial_port.read(1024)
                    if data:
                        buffer += data.decode('utf-8', errors='ignore')
                        while '\n' in buffer:
                            line, buffer = buffer.split('\n', 1)
                            line = line.strip()
                            if line:
                                self.parse_line(line)
                    else:
                        time.sleep(0.001)
                except serial.SerialException as e:
                    self.status_message.emit(f"Serial Error: {str(e)}")
                    break
        except Exception as e:
            self.status_message.emit(f"Connection Error: {str(e)}")
        finally:
            if self.serial_port and self.serial_port.is_open:
                self.serial_port.close()
            self.connection_status.emit(False)

    def parse_line(self, line):
        parsed = parse_telemetry_line(line)
        if parsed:
            self.data_received.emit(
                parsed["node_id"],
                parsed["timestamp_us"],
                parsed["x"],
                parsed["y"],
                parsed["z"],
                parsed["status_int"]
            )
        else:
            if line and line.strip():
                self.status_message.emit(f"MCU: {line.strip()}")

    def stop(self):
        self.running = False
        if self.serial_port and self.serial_port.is_open:
            try:
                self.serial_port.cancel_read()
            except Exception:
                pass

    def run_mock(self):
        self.running = True
        self.connection_status.emit(True)
        self.status_message.emit("Running Mock Sensor Data Stream")
        t = 0
        while self.running:
            if self.streaming:
                bx = 20000.0 + 50.0 * math.sin(t * 0.1) + random.uniform(-2, 2)
                by = -4000.0 + 30.0 * math.cos(t * 0.1) + random.uniform(-2, 2)
                bz = 45000.0 + 100.0 * math.sin(t * 0.05) + random.uniform(-3, 3)
                status = 0xC00000
                self.data_received.emit("MOCK_NODE", t * 1000000.0, bx, by, bz, status)
                t += 0.01
            time.sleep(0.01)
        self.connection_status.emit(False)
