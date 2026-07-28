import socket
import time
from PySide6.QtCore import QThread, Signal

class UdpWorker(QThread):
    data_received = Signal(object, object, object, object, object)  # timestamp, x, y, z, status
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
        # Expected format: timestamp_us,x,y,z[,status_hex]
        try:
            parts = line.split(',')
            if len(parts) >= 4:
                ts = float(parts[0]) # Raw microseconds or float seconds
                x = int(parts[1])
                y = int(parts[2])
                z = int(parts[3])
                status = 0xC00000
                if len(parts) >= 5:
                    try:
                        status = int(parts[4], 16)
                    except ValueError:
                        status = 0xC00000
                self.data_received.emit(ts, x, y, z, status)
        except (ValueError, IndexError):
            # Not a numeric data line, treat as MCU status message
            self.status_message.emit(f"MCU (WiFi): {line}")

    def stop(self):
        self.running = False

    def send_command(self, cmd):
        if self.sock and self.target_ip:
            try:
                msg = f"{cmd}\n".encode('utf-8')
                self.sock.sendto(msg, (self.target_ip, self.target_port))
                self.status_message.emit(f"Sent UDP command to {self.target_ip}: {cmd}")
            except Exception as e:
                self.status_message.emit(f"Failed to send UDP command: {str(e)}")
        else:
            self.status_message.emit("Cannot send UDP command: Target IP not discovered yet.")
