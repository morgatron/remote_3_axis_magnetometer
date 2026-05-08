import math
import random
import time
import serial
import serial.tools.list_ports
from PySide6.QtCore import QThread, Signal

class SerialWorker(QThread):
    data_received = Signal(float, int, int, int)  # timestamp, x, y, z
    status_message = Signal(str)
    connection_status = Signal(bool)

    def __init__(self, port, baudrate=115200):
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

            buffer = b""
            while self.running:
                if self.serial_port.in_waiting:
                    new_data = self.serial_port.read(self.serial_port.in_waiting)
                    buffer += new_data

                    while b'\n' in buffer:
                        line_bytes, buffer = buffer.split(b'\n', 1)
                        line = line_bytes.decode('utf-8', errors='ignore').strip()
                        if line:
                            self.parse_line(line)
                else:
                    time.sleep(0.001)

        except Exception as e:
            self.status_message.emit(f"Error: {str(e)}")
            self.connection_status.emit(False)
        finally:
            if self.serial_port and self.serial_port.is_open:
                self.serial_port.close()
            self.connection_status.emit(False)

    def run_mock(self):
        self.running = True
        self.connection_status.emit(True)
        self.status_message.emit("Simulator Mode Active")

        start_time = time.perf_counter()
        sample_period = 1.0 / 37.0 # Default ~37Hz

        while self.running:
            if self.streaming:
                now = time.perf_counter()
                elapsed = now - start_time
                ts_us = float(int(elapsed * 1000000))

                # Synthetic Signals
                # X: 5Hz Sine Wave (Clear PSD Peak)
                x = int(10000 * math.sin(2 * math.pi * 5 * elapsed))
                # Y: 12Hz Sine + Noise
                y = int(5000 * math.sin(2 * math.pi * 12 * elapsed) + random.gauss(0, 500))
                # Z: Static + Noise
                z = int(20000 + random.gauss(0, 200))

                self.data_received.emit(ts_us, x, y, z)

            time.sleep(sample_period)

    def parse_line(self, line):
        # Expected format: timestamp_us,x,y,z
        try:
            parts = line.split(',')
            if len(parts) == 4:
                ts = float(parts[0]) # Raw microseconds
                x = int(parts[1])
                y = int(parts[2])
                z = int(parts[3])
                self.data_received.emit(ts, x, y, z)
        except (ValueError, IndexError):
            # Not a data line, might be a status message or help text
            self.status_message.emit(f"MCU: {line}")

    def stop(self):
        self.running = False

    def send_command(self, cmd):
        if self.port == "MOCK_SENSOR":
            cmd_up = cmd.strip().upper()
            if cmd_up == "STREAM ON":
                self.streaming = True
                self.status_message.emit("MCU: Streaming enabled.")
            elif cmd_up == "STREAM OFF":
                self.streaming = False
                self.status_message.emit("MCU: Streaming disabled.")
            elif cmd_up.startswith("RATE "):
                self.status_message.emit(f"MCU: Rate set to {cmd[5:]}")
            return

        if self.serial_port and self.serial_port.is_open:
            self.serial_port.write(f"{cmd}\n".encode('utf-8'))
