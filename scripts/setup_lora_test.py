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

def provision_sensor(port, batch_size=10):
    print(f"\n--- Provisioning Heltec V4 Sensor Node on {port} (Batch Size: {batch_size}) ---")
    s = serial.Serial(port, 921600, timeout=1.0)
    time.sleep(1.0)
    s.read_all()

    # Explicitly configure MOCK sensor, BATCH size, and LORA mode (No BLE, No Wi-Fi)
    cmds = [
        "SENSOR MOCK",
        "ID NODE_LORA_TEST",
        f"BATCH {batch_size}",
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

def monitor_lora_test(sensor_port, rcvr_port, duration=35):
    print("\n========================================================")
    print(f" 2. Monitoring Over-The-Air LoRa Reception on {rcvr_port} ({duration}s)")
    print("========================================================")

    s_rcvr = serial.Serial(rcvr_port, 921600, timeout=1.0)
    time.sleep(1.0)
    s_rcvr.read_all()

    t0 = time.time()
    packet_count = 0
    batch_count = 0
    last_burst_time = None

    while time.time() - t0 < duration:
        out = s_rcvr.read_all().decode('utf-8', errors='ignore')
        if out:
            lines = [l.strip() for l in out.splitlines() if l.strip()]
            burst_samples = []
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
                            rssi = parts[8] if len(parts) >= 9 else "N/A"
                            burst_samples.append((dev_id, ts, bx, by, bz, bmag, rssi))
                        except Exception:
                            print(f"[{time.time()-t0:05.1f}s] LoRa RAW: {line}")
                    elif "LORA" in line or "Radio" in line or "SUCCESS" in line:
                        print(f"[{time.time()-t0:05.1f}s] Receiver Log: {line}")

            if burst_samples:
                now_s = time.time() - t0
                delta_str = f" (+{now_s - last_burst_time:.1f}s since last burst)" if last_burst_time is not None else ""
                last_burst_time = now_s
                batch_count += 1
                sample_cnt = len(burst_samples)
                first_s = burst_samples[0]
                last_s = burst_samples[-1]
                print(f"\n[{now_s:05.1f}s] >>> LoRa Burst #{batch_count:02d} Received: {sample_cnt} sample(s){delta_str} | RSSI: {first_s[6]} dBm <<<")
                for idx, s_info in enumerate(burst_samples, 1):
                    print(f"   [{idx:02d}/{sample_cnt:02d}] Node: {s_info[0]} | TS: {s_info[1]} | B=({s_info[2]:+.1f}, {s_info[3]:+.1f}, {s_info[4]:+.1f}) nT | |B|={s_info[5]:.1f} nT")
        time.sleep(0.1)

    s_rcvr.close()
    print("\n========================================================")
    print(f" LoRa Test Complete. Total Packets = {packet_count} across {batch_count} burst(s)")
    print("========================================================\n")

def main():
    parser = argparse.ArgumentParser(description="Heltec V4 SX1262 LoRa Setup & Test Tool (AU915 Band)")
    parser.add_argument("--sensor-port", default="/dev/ttyACM1", help="Serial port for Sensor Heltec V4 (default: /dev/ttyACM1)")
    parser.add_argument("--rcvr-port", default="/dev/ttyACM0", help="Serial port for Receiver Heltec V4 (default: /dev/ttyACM0)")
    parser.add_argument("--batch", type=int, default=10, help="Batch burst size in samples (default: 10 = 1 burst every 10s)")
    parser.add_argument("--skip-flash", action="store_true", help="Skip firmware flashing step")
    parser.add_argument("--duration", type=int, default=35, help="Test monitoring duration in seconds (default: 35)")
    args = parser.parse_args()

    sensor_port = args.sensor_port
    rcvr_port = args.rcvr_port

    print(f"[SETUP] Receiver Port: {rcvr_port} | Sensor Port: {sensor_port} | Batch Size: {args.batch} samples")

    if not args.skip_flash:
        flash_nodes(sensor_port, rcvr_port)

    provision_receiver(rcvr_port)
    provision_sensor(sensor_port, batch_size=args.batch)
    monitor_lora_test(sensor_port, rcvr_port, duration=args.duration)

if __name__ == "__main__":
    main()
