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

def get_pio_cmd():
    import shutil
    if shutil.which("pio"):
        return "pio"
    return "~/.platformio/penv/bin/pio"

def flash_nodes(sensor_port, rcvr_port):
    print("\n========================================================")
    print(" 1. Flashing Firmware to Heltec V4 Target Boards")
    print("========================================================")
    
    pio = get_pio_cmd()
    print(f"Flashing Receiver firmware (heltec_v4_receiver) to {rcvr_port}...")
    if not run_cmd(f"{pio} run -e heltec_v4_receiver -t upload --upload-port {rcvr_port}"):
        sys.exit(1)

    print(f"Flashing Sensor firmware (heltec_v4_sensor) to {sensor_port}...")
    if not run_cmd(f"{pio} run -e heltec_v4_sensor -t upload --upload-port {sensor_port}"):
        sys.exit(1)

def provision_sensor(port):
    print(f"\n--- Provisioning Heltec V4 Sensor Node on {port} ---")
    s = serial.Serial(port, 921600, timeout=1.0)
    time.sleep(1.0)
    s.read_all()

    # Explicitly configure MOCK sensor and LORA mode (No BLE, No Wi-Fi)
    cmds = [
        "SENSOR MOCK",
        "ID NODE_LORA_TEST",
        "MODE LORA",
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
    print(f" 2. Monitoring Over-The-Air LoRa Reception on {rcvr_port} ({duration}s)")
    print("========================================================")

    s_rcvr = serial.Serial(rcvr_port, 921600, timeout=1.0)
    time.sleep(1.0)
    s_rcvr.read_all()

    t0 = time.time()
    packet_count = 0

    while time.time() - t0 < duration:
        out = s_rcvr.read_all().decode('utf-8', errors='ignore')
        if out:
            lines = [l.strip() for l in out.splitlines() if l.strip()]
            for line in lines:
                if any(line.startswith(pfx) for pfx in ['NODE_', 'LORA_', 'MOCK_', 'SENSOR_']) or ',' in line:
                    parts = line.split(',')
                    if len(parts) >= 5:
                        packet_count += 1
                        try:
                            dev_id = parts[0]
                            ts = parts[1]
                            bx, by, bz = float(parts[2]), float(parts[3]), float(parts[4])
                            bmag = (bx**2 + by**2 + bz**2)**0.5
                            rssi_str = f" | RSSI: {parts[8]} dBm" if len(parts) >= 9 else ""
                            print(f"[{time.time()-t0:05.1f}s] LoRa RX #{packet_count:03d}: {dev_id} | B=({bx:+.1f}, {by:+.1f}, {bz:+.1f}) nT | |B|={bmag:.1f} nT{rssi_str}")
                        except Exception:
                            print(f"[{time.time()-t0:05.1f}s] LoRa RAW: {line}")
                    elif "LORA" in line or "Radio" in line or "SUCCESS" in line:
                        print(f"[{time.time()-t0:05.1f}s] Receiver Log: {line}")
        time.sleep(0.1)

    s_rcvr.close()
    print("\n========================================================")
    print(f" LoRa Test Complete. Total LoRa Packets Received = {packet_count}")
    print("========================================================\n")

def main():
    parser = argparse.ArgumentParser(description="Heltec V4 SX1262 LoRa Setup & Test Tool (AU915 Band)")
    parser.add_argument("--sensor-port", default="/dev/ttyACM1", help="Serial port for Sensor Heltec V4 (default: /dev/ttyACM1)")
    parser.add_argument("--rcvr-port", default="/dev/ttyACM0", help="Serial port for Receiver Heltec V4 (default: /dev/ttyACM0)")
    parser.add_argument("--skip-flash", action="store_true", help="Skip firmware flashing step")
    parser.add_argument("--duration", type=int, default=30, help="Test monitoring duration in seconds (default: 30)")
    args = parser.parse_args()

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
