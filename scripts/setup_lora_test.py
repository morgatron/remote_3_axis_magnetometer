#!/usr/bin/env python3
"""
Automated Provisioning and Over-The-Air Test Tool for Heltec V4 SX1262 LoRa Setup
Target Band: AU915 (915.0 MHz)
"""

import sys
import time
import argparse
import subprocess
import serial
import serial.tools.list_ports

def find_ports():
    ports = sorted([p.device for p in serial.tools.list_ports.comports() if 'ACM' in p.device or 'USB' in p.device])
    return ports

def run_cmd(cmd):
    print(f"[EXEC] {cmd}")
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[ERROR] Command failed:\n{res.stderr}")
        return False
    return True

def flash_nodes(sensor_port, rcvr_port):
    print("\n========================================================")
    print(" 1. Flashing Firmware to Heltec V4 Target Boards")
    print("========================================================")
    
    print(f"Flashing Receiver firmware (heltec_v4_receiver) to {rcvr_port}...")
    if not run_cmd(f"~/.platformio/penv/bin/pio run -e heltec_v4_receiver -t upload --upload-port {rcvr_port}"):
        sys.exit(1)

    print(f"Flashing Sensor firmware (heltec_v4_sensor) to {sensor_port}...")
    if not run_cmd(f"~/.platformio/penv/bin/pio run -e heltec_v4_sensor -t upload --upload-port {sensor_port}"):
        sys.exit(1)

def provision_sensor(port):
    print(f"\n--- Provisioning Heltec V4 Sensor Node on {port} ---")
    s = serial.Serial(port, 921600, timeout=1.0)
    time.sleep(1.0)
    s.read_all()

    cmds = [
        "SENSOR MOCK",
        "ID NODE_LORA_TEST",
        "BATCH 10",
        "MODE BOTH",
        "STREAM ON",
        "SAVE"
    ]
    for cmd in cmds:
        s.write(f"\r\n{cmd}\r\n".encode('utf-8'))
        time.sleep(0.3)
    
    output = s.read_all().decode('utf-8', errors='ignore')
    print("Sensor CLI Response:\n", output)
    s.close()

def provision_receiver(port):
    print(f"\n--- Provisioning Heltec V4 Gateway Receiver Node on {port} ---")
    s = serial.Serial(port, 921600, timeout=1.0)
    time.sleep(1.0)
    s.read_all()

    cmds = [
        "MODE SERIAL",
        "SAVE"
    ]
    for cmd in cmds:
        s.write(f"\r\n{cmd}\r\n".encode('utf-8'))
        time.sleep(0.3)
    
    output = s.read_all().decode('utf-8', errors='ignore')
    print("Receiver CLI Response:\n", output)
    s.close()

def monitor_lora_test(sensor_port, rcvr_port, duration=30):
    print("\n========================================================")
    print(f" 2. Monitoring Over-The-Air LoRa Reception ({duration}s)")
    print("========================================================")

    s_rcvr = serial.Serial(rcvr_port, 921600, timeout=1.0)
    time.sleep(1.0)
    s_rcvr.read_all()

    t0 = time.time()
    packet_count = 0

    while time.time() - t0 < duration:
        out = s_rcvr.read_all().decode('utf-8', errors='ignore')
        if out:
            lines = [l.strip() for l in out.splitlines() if l.strip().startswith('NODE_') or l.strip().startswith('LORA_') or l.strip().startswith('MOCK_')]
            for line in lines:
                packet_count += 1
                print(f"[{time.time()-t0:04.1f}s] RX #{packet_count:02d}: {line}")
        time.sleep(0.3)

    s_rcvr.close()
    print("\n========================================================")
    print(f" LoRa Test Complete. Total Packets Received = {packet_count}")
    print("========================================================\n")

def main():
    parser = argparse.ArgumentParser(description="Heltec V4 SX1262 LoRa Setup & Test Tool (AU915 Band)")
    parser.add_argument("--sensor-port", help="Serial port for Sensor Heltec V4 (e.g. /dev/ttyACM1)")
    parser.add_argument("--rcvr-port", help="Serial port for Receiver Heltec V4 (e.g. /dev/ttyACM0)")
    parser.add_argument("--skip-flash", action="store_true", help="Skip firmware flashing step")
    parser.add_argument("--duration", type=int, default=30, help="Test monitoring duration in seconds")
    args = parser.parse_args()

    ports = find_ports()
    if not args.sensor_port or not args.rcvr_port:
        if len(ports) < 2:
            print(f"[ERROR] Found {len(ports)} serial ports ({ports}). Need 2 Heltec V4 devices connected.")
            sys.exit(1)
        sensor_port = args.sensor_port or (ports[1] if len(ports) > 1 else ports[0])
        rcvr_port = args.rcvr_port or ports[0]
    else:
        sensor_port = args.sensor_port
        rcvr_port = args.rcvr_port

    print(f"[SETUP] Receiver Port: {rcvr_port} | Sensor Port: {sensor_port}")

    if not args.skip_flash:
        flash_nodes(sensor_port, rcvr_port)

    provision_receiver(rcvr_port)
    provision_sensor(sensor_port)
    monitor_lora_test(sensor_port, rcvr_port, duration=args.duration)

if __name__ == "__main__":
    main()
