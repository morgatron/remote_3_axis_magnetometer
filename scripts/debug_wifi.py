#!/usr/bin/env python3
import serial
import time
import sys
import socket

def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

print(f"Local Host IP: {get_local_ip()}")
print("Connecting to ESP32 over /dev/ttyUSB0...")

ser = serial.Serial('/dev/ttyUSB0', 921600, timeout=1)
ser.reset_input_buffer()
time.sleep(1)

def send_cmd(cmd):
    print(f"\n---> Sending: {cmd}")
    ser.write((cmd + "\r\n").encode('utf-8'))
    time.sleep(0.5)
    start = time.time()
    while time.time() - start < 3:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"  [MCU] {line}")
        else:
            time.sleep(0.1)

send_cmd("WIFI STATUS")
send_cmd("STATUS")

ser.close()
