#!/usr/bin/env python3
import serial
import time
import sys
import os

BAUD = 921600

def get_port():
    if os.path.exists('/dev/ttyUSB0'):
        return '/dev/ttyUSB0'
    elif os.path.exists('/dev/ttyACM0'):
        return '/dev/ttyACM0'
    return '/dev/ttyUSB0'

def verify(duration_sec=30):
    port = get_port()
    print(f"Opening {port} at {BAUD} baud...")
    ser = serial.Serial(port, BAUD, timeout=1)
    ser.reset_input_buffer()

    print("Waiting 1s for boot header to pass...")
    time.sleep(1)
    ser.reset_input_buffer()

    start_time = time.time()
    total_samples = 0
    anomalies = 0
    prev_x, prev_y, prev_z = None, None, None

    print(f"Monitoring active CSV datastream on {port} for {duration_sec} seconds...")

    while (time.time() - start_time) < duration_sec:
        line = ser.readline().decode('ascii', errors='ignore').strip()
        if not line or line.startswith("=") or line.startswith("FIRMWARE") or line.startswith("timestamp") or line.startswith("Sensor") or line.startswith("---") or line.startswith("HELP") or line.startswith("STREAM") or line.startswith("RATE") or line.startswith("CYCLE") or line.startswith("STATUS") or line.startswith("Device") or line.startswith("CONFIG") or line.startswith("CH") or line.startswith("VREF") or line.startswith("Gain") or line.startswith("Raw"):
            continue

        parts = line.split(',')
        if len(parts) == 6:
            device_id = parts[0]
            ts_str, x_str, y_str, z_str, status_hex = parts[1], parts[2], parts[3], parts[4], parts[5]
        elif len(parts) == 5:
            device_id = "LOCAL"
            ts_str, x_str, y_str, z_str, status_hex = parts[0], parts[1], parts[2], parts[3], parts[4]
        else:
            anomalies += 1
            print(f"[MALFORMED LINE #{total_samples}] '{line}'")
            continue

        try:
            ts = int(ts_str)
            x = int(x_str)
            y = int(y_str)
            z = int(z_str)
            status_val = int(status_hex, 16)
        except ValueError as e:
            anomalies += 1
            print(f"[PARSE ERROR #{total_samples}] '{line}' -> {e}")
            continue

        total_samples += 1

        # Check status header validity (must start with 0xC)
        if (status_val & 0xF00000) != 0xC00000:
            anomalies += 1
            print(f"[INVALID STATUS #{total_samples}] status=0x{status_hex}")

        # Check all 0s
        if x == 0 and y == 0 and z == 0:
            anomalies += 1
            print(f"[ZERO READ #{total_samples}] x=0, y=0, z=0")

        # Check spikes (> 300 nT jump)
        if prev_x is not None:
            dx = abs(x - prev_x)
            dy = abs(y - prev_y)
            dz = abs(z - prev_z)
            if dx > 300 or dy > 300 or dz > 300:
                anomalies += 1
                print(f"[DATA SPIKE #{total_samples}] dx={dx}, dy={dy}, dz={dz} (x={x}, y={y}, z={z})")

        prev_x, prev_y, prev_z = x, y, z

    ser.close()
    elapsed = time.time() - start_time
    rate = total_samples / elapsed
    print(f"\nVerification Results:")
    print(f"  Port:              {port}")
    print(f"  Processed Samples: {total_samples}")
    print(f"  Streaming Rate:    {rate:.2f} Hz")
    print(f"  Total Anomalies:   {anomalies}")

if __name__ == '__main__':
    verify(30)
