import serial
import time

ser = serial.Serial('/dev/ttyUSB0', 921600, timeout=2)
time.sleep(1)

def send_cmd(cmd):
    print(f"Sending: {cmd}")
    ser.write((cmd + "\r\n").encode('utf-8'))
    time.sleep(0.5)
    while ser.in_waiting:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        print(f"  [MCU] {line}")

send_cmd("WIFI STATUS")
send_cmd("STATUS")
ser.close()
