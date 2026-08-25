#!/usr/bin/env python3
"""
Real-Time Telemetry Stream Monitor (`stream_monitor.py`)

Listens to serial telemetry streams from magnetometer field sensor nodes or
receiver gateways, parses 6-column and 9-column CSV packets, and displays
live magnetic field components, total magnitude, battery voltage, and stream stats.

Usage:
  python3 scripts/stream_monitor.py
  python3 scripts/stream_monitor.py --port /dev/ttyACM0 --duration 60
  ./scripts/stream_monitor.py --port /dev/ttyACM0
"""

import sys
import os
import time
import math
import argparse

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Error: 'pyserial' package is required. Install via: pip install pyserial")
    sys.exit(1)


def find_serial_ports():
    """Finds available serial ports for ESP32 devices."""
    ports = sorted([p.device for p in serial.tools.list_ports.comports() if 'ACM' in p.device or 'USB' in p.device])
    return ports


def get_default_port():
    ports = find_serial_ports()
    if ports:
        return ports[0]
    return '/dev/ttyACM0'


def monitor_stream(port, baud=921600, duration_sec=120, show_raw=False, csv_file=None):
    if not os.path.exists(port):
        ports = find_serial_ports()
        if ports:
            port = ports[0]
            print(f"[PORT NOTICE] Auto-selected available port: {port}")
        else:
            print(f"[ERROR] Serial port '{port}' not found and no other serial devices detected.")
            sys.exit(1)

    print("\n" + "=" * 92)
    print("              REMOTE 3-AXIS MAGNETOMETER TELEMETRY MONITOR")
    print("=" * 92)
    print(f"  Target Port:     {port} @ {baud} baud")
    print(f"  Duration:        {duration_sec} seconds (Press Ctrl+C to stop)")
    if csv_file:
        print(f"  Log File:        {csv_file}")
    print("=" * 92 + "\n")

    try:
        ser = serial.Serial(port, baud, timeout=1.0)
    except Exception as e:
        print(f"[ERROR] Could not open serial port {port}: {e}")
        sys.exit(1)

    ser.reset_input_buffer()

    # Column header
    header_fmt = "{:>6} | {:<12} | {:>12} | {:>10} | {:>10} | {:>10} | {:>10} | {:>6} | {:>5} | {:<8}"
    row_fmt    = "{:>6} | {:<12} | {:>12} | {:>10.2f} | {:>10.2f} | {:>10.2f} | {:>10.2f} | {:>6.2f} | {:>5} | {:<8}"

    print(header_fmt.format("SAMPLE", "NODE_ID", "TIMESTAMP_US", "Bx (nT)", "By (nT)", "Bz (nT)", "|B| (nT)", "VBAT", "RSSI", "STATUS"))
    print("-" * 92)

    csv_handle = None
    if csv_file:
        try:
            csv_handle = open(csv_file, "a", encoding="utf-8")
        except Exception as e:
            print(f"[WARNING] Could not open CSV log file: {e}")

    start_time = time.time()
    last_print_time = start_time
    total_samples = 0
    anomalies = 0
    magnitudes = []

    try:
        while (time.time() - start_time) < duration_sec:
            raw_line = ser.readline().decode('utf-8', errors='ignore').strip()
            if not raw_line:
                continue

            if show_raw:
                print(f"[RAW] {raw_line}")

            # Filter out non-CSV debug banners
            if raw_line.startswith("=") or raw_line.startswith("FIRMWARE") or raw_line.startswith("---") or \
               raw_line.startswith("HELP") or raw_line.startswith("Device") or raw_line.startswith("Sensor") or \
               raw_line.startswith("Rate") or raw_line.startswith("Active") or raw_line.startswith("device_id"):
                continue

            parts = raw_line.split(',')
            if len(parts) < 5:
                continue

            node_id = "LOCAL"
            ts = 0
            x, y, z = 0.0, 0.0, 0.0
            status_hex = "000000"
            vbat = 0.0
            rssi = 0

            try:
                if len(parts) >= 9:
                    # 9-column receiver egress: node_id,ts,x,y,z,status,temp,vbat,rssi
                    node_id = parts[0].strip()
                    ts = int(parts[1])
                    x = float(parts[2])
                    y = float(parts[3])
                    z = float(parts[4])
                    status_hex = parts[5].strip()
                    vbat = float(parts[7])
                    rssi = int(parts[8])
                elif len(parts) >= 6:
                    # 6-column sensor stream: node_id,ts,x,y,z,status
                    node_id = parts[0].strip()
                    ts = int(parts[1])
                    x = float(parts[2])
                    y = float(parts[3])
                    z = float(parts[4])
                    status_hex = parts[5].strip()
                elif len(parts) == 5:
                    # Legacy 5-column: ts,x,y,z,status
                    ts = int(parts[0])
                    x = float(parts[1])
                    y = float(parts[2])
                    z = float(parts[3])
                    status_hex = parts[4].strip()
            except ValueError:
                anomalies += 1
                continue

            total_samples += 1
            mag = math.sqrt(x*x + y*y + z*z)
            magnitudes.append(mag)

            vbat_str = f"{vbat:.2f}V" if vbat > 0 else "--"
            rssi_str = f"{rssi}dBm" if rssi != 0 else "--"

            print(row_fmt.format(
                total_samples,
                node_id[:12],
                ts,
                x, y, z, mag,
                vbat,
                rssi if rssi != 0 else 0,
                status_hex
            ))

            if csv_handle:
                csv_handle.write(f"{node_id},{ts},{x:.2f},{y:.2f},{z:.2f},{mag:.2f},{status_hex},{vbat:.2f},{rssi}\n")
                csv_handle.flush()

    except KeyboardInterrupt:
        print("\n[INFO] Monitoring stopped by user.")
    finally:
        ser.close()
        if csv_handle:
            csv_handle.close()

    elapsed = time.time() - start_time
    rate = (total_samples / elapsed) if elapsed > 0 else 0
    avg_mag = (sum(magnitudes) / len(magnitudes)) if magnitudes else 0

    print("\n" + "=" * 92)
    print("                         TELEMETRY STREAM SUMMARY")
    print("=" * 92)
    print(f"  Duration:          {elapsed:.2f} seconds")
    print(f"  Total Packets:     {total_samples} samples")
    print(f"  Effective Rate:    {rate:.2f} Hz")
    if magnitudes:
        print(f"  Mean Field |B|:    {avg_mag:.2f} nT (Min: {min(magnitudes):.2f}, Max: {max(magnitudes):.2f})")
    print(f"  Anomalies/Drops:   {anomalies}")
    print("=" * 92 + "\n")


def main():
    parser = argparse.ArgumentParser(
        description="Monitor real-time telemetry from remote 3-axis magnetometer nodes."
    )
    parser.add_argument("--port", type=str, default=get_default_port(), help="Serial port (default: auto-detected)")
    parser.add_argument("--baud", type=int, default=921600, help="Baud rate (default: 921600)")
    parser.add_argument("-d", "--duration", type=int, default=120, help="Capture duration in seconds (default: 120)")
    parser.add_argument("-r", "--raw", action="store_true", help="Print raw unparsed serial lines")
    parser.add_argument("-o", "--csv", type=str, help="Save parsed telemetry to CSV file")

    # Allow positional integer duration for backwards compatibility: ./stream_monitor.py 60
    if len(sys.argv) == 2 and sys.argv[1].isdigit():
        sys.argv = [sys.argv[0], "-d", sys.argv[1]]

    args = parser.parse_args()
    monitor_stream(args.port, args.baud, args.duration, args.raw, args.csv)


if __name__ == "__main__":
    main()

