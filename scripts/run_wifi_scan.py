import serial
import time

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

send_cmd("WIFI SCAN", wait_time=5.0)
ser.close()
