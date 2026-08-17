import serial
import time
import socket

def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "192.168.20.8"

host_ip = get_local_ip()
print(f"Host Laptop IP: {host_ip}")

ser = serial.Serial('/dev/ttyUSB0', 921600, timeout=1)
ser.reset_input_buffer()
time.sleep(1)

def send_cmd(cmd, wait_time=2.0):
    print(f"\n---> Sending: {cmd}")
    ser.write((cmd + "\r\n").encode('utf-8'))
    start = time.time()
    while time.time() - start < wait_time:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line and not line.startswith("NODE_"):
                print(f"  [MCU] {line}")
        else:
            time.sleep(0.05)

send_cmd("MODE BOTH", wait_time=1.0)
send_cmd("TARGET " + host_ip, wait_time=1.0)
send_cmd("WIFI NetComm 2927 zakuystuhs", wait_time=9.0)
send_cmd("WIFI STATUS", wait_time=2.0)

ser.close()
