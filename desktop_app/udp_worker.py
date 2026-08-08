import os
import sys
import socket
import time
from PySide6.QtCore import QThread, Signal

# Import shared stream parser from repository root
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from stream_parser import parse_telemetry_line

class UdpWorker(QThread):
    data_received = Signal(str, object, object, object, object, object)  # device_id, timestamp, x, y, z, status
    status_message = Signal(str)
    connection_status = Signal(bool)

    def __init__(self, listen_port=9876, target_ip=None, target_port=9876):
        super().__init__()
        self.listen_port = listen_port
        self.target_ip = target_ip
        self.target_port = target_port
        self.running = False
        self.sock = None

    def run(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.sock.bind(('0.0.0.0', self.listen_port))
            self.sock.settimeout(0.5)
            self.running = True
            self.connection_status.emit(True)
            self.status_message.emit(f"UDP Receiver listening on port {self.listen_port}")

            while self.running:
                try:
                    data, addr = self.sock.recvfrom(2048)
                    if not self.target_ip:
                        self.target_ip = addr[0]  # Auto-discover target IP from first incoming packet
                    
                    text = data.decode('utf-8', errors='ignore').strip()
                    for line in text.splitlines():
                        line = line.strip()
                        if line:
                            self.parse_line(line)
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running:
                        self.status_message.emit(f"UDP Error: {str(e)}")

        except Exception as e:
            self.status_message.emit(f"UDP Socket Setup Error: {str(e)}")
            self.connection_status.emit(False)
        finally:
            if self.sock:
                self.sock.close()
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
                self.status_message.emit(f"MCU (WiFi): {line.strip()}")

    def stop(self):
        self.running = False
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
