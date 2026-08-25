#!/usr/bin/env python3
"""
Real-Time Telemetry & RSSI Signal Strength Monitor (`rssi_monitor.py`)

Specialized diagnostic monitor for evaluating RF link quality (LoRa / BLE / ESP-NOW)
and magnetometer telemetry. Supports both desktop serial ports (/dev/ttyACM*) and
direct Android Termux USB file descriptors via 'termux-usb'.

Features:
- Live visual RSSI signal quality bars (dBm & percentage rating)
- Real-time magnetic field magnitude |B| and battery voltage
- Node tracking with running min/max/average RSSI statistics
- Direct USBDEVFS ioctl (Termux) or standard pyserial (Desktop)
"""

import sys
import os
import time
import math
import argparse
from typing import Optional, Dict, Any

# Ensure line-buffered stdout for responsive terminal output
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(line_buffering=True)

# Try importing shared parser and termux USB driver
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "central_service")))
sys.path.insert(1, os.path.dirname(__file__))

try:
    from stream_parser import parse_telemetry_line
except ImportError:
    def parse_telemetry_line(line: str) -> Optional[Dict[str, Any]]:
        parts = line.strip().split(",")
        if len(parts) >= 6:
            try:
                temp = float(parts[6]) if len(parts) >= 7 and parts[6].strip() else None
                vbat = float(parts[7]) if len(parts) >= 8 and parts[7].strip() else None
                rssi = int(float(parts[8])) if len(parts) >= 9 and parts[8].strip() else None
                return {
                    "node_id": parts[0].strip(),
                    "timestamp_us": float(parts[1]),
                    "x": float(parts[2]),
                    "y": float(parts[3]),
                    "z": float(parts[4]),
                    "status_hex": parts[5].strip(),
                    "temp": temp,
                    "vbat": vbat,
                    "rssi": rssi
                }
            except Exception:
                return None
        return None

try:
    from termux_usb_driver import TermuxUsbDevice
    HAS_TERMUX_USB_DRIVER = True
except ImportError:
    HAS_TERMUX_USB_DRIVER = False


def format_rssi_bar(rssi: Optional[int], width: int = 10) -> str:
    """Renders a text-based signal strength bar from RSSI dBm."""
    if rssi is None or rssi == 0:
        return "[" + " " * width + "] (No RSSI)"
    
    # Typical RF range: -115 dBm (poor) to -35 dBm (excellent)
    min_dbm = -115
    max_dbm = -35
    clamped = max(min(rssi, max_dbm), min_dbm)
    fraction = (clamped - min_dbm) / float(max_dbm - min_dbm)
    filled = int(round(fraction * width))
    bar = "█" * filled + "░" * (width - filled)
    
    if rssi >= -65:
        qual = "Excellent"
    elif rssi >= -80:
        qual = "Good     "
    elif rssi >= -95:
        qual = "Fair     "
    elif rssi >= -105:
        qual = "Weak     "
    else:
        qual = "Critical "
        
    return f"[{bar}] {qual}"


class StreamStats:
    def __init__(self):
        self.start_time = time.time()
        self.node_stats = {}  # node_id -> {count, rssi_list, mag_list, last_vbat}
        self.total_samples = 0

    def record(self, sample: Dict[str, Any]):
        self.total_samples += 1
        nid = sample["node_id"]
        if nid not in self.node_stats:
            self.node_stats[nid] = {
                "count": 0,
                "rssi_list": [],
                "mag_list": [],
                "last_vbat": sample.get("vbat"),
                "first_seen": time.time()
            }
        
        entry = self.node_stats[nid]
        entry["count"] += 1
        if sample.get("vbat") is not None:
            entry["last_vbat"] = sample["vbat"]
            
        mag = math.sqrt(sample["x"]**2 + sample["y"]**2 + sample["z"]**2)
        entry["mag_list"].append(mag)
        
        rssi = sample.get("rssi")
        if rssi is not None and rssi != 0:
            entry["rssi_list"].append(rssi)

    def print_summary(self):
        elapsed = time.time() - self.start_time
        print("\n" + "=" * 90)
        print("                       RSSI & TELEMETRY SESSION SUMMARY")
        print("=" * 90)
        print(f"  Duration:         {elapsed:.1f} seconds")
        print(f"  Total Samples:    {self.total_samples}")
        if elapsed > 0:
            print(f"  Throughput:       {self.total_samples / elapsed:.2f} Hz")
        print("-" * 90)
        print(f" {'NODE ID':<12} | {'SAMPLES':<8} | {'AVG RSSI':<10} | {'MIN / MAX RSSI':<16} | {'AVG |B|':<12} | {'LAST VBAT'}")
        print("-" * 90)
        
        for nid, stats in sorted(self.node_stats.items()):
            cnt = stats["count"]
            rssi_vals = stats["rssi_list"]
            if rssi_vals:
                avg_rssi = f"{sum(rssi_vals)/len(rssi_vals):.1f} dBm"
                min_max_rssi = f"{min(rssi_vals)} / {max(rssi_vals)} dBm"
            else:
                avg_rssi = "N/A"
                min_max_rssi = "N/A"
                
            mag_vals = stats["mag_list"]
            avg_mag = f"{sum(mag_vals)/len(mag_vals):.1f} nT" if mag_vals else "N/A"
            vbat_str = f"{stats['last_vbat']:.2f} V" if stats['last_vbat'] else "N/A"
            
            print(f" {nid:<12} | {cnt:<8} | {avg_rssi:<10} | {min_max_rssi:<16} | {avg_mag:<12} | {vbat_str}")
        print("=" * 90 + "\n")


def run_termux_usb_monitor(fd: int, baud: int = 921600):
    """Direct Termux USB reader using USBDEVFS ioctl bulk transfer."""
    if not HAS_TERMUX_USB_DRIVER:
        print("[ERROR] 'termux_usb_driver' module not found.")
        return

    try:
        dev = TermuxUsbDevice(fd=fd, baudrate=baud)
    except Exception as e:
        print(f"[ERROR] Failed to initialize Termux USB device on FD {fd}: {e}")
        return

    print(f"\n[Termux USB] Connected: {dev.chipset}")
    print(f"             Bulk IN EP: 0x{dev.in_ep:02X} | Baud: {baud}")
    print(f"             Control lines: DTR=1, RTS=0 (Normal Run)")
    print(f"[Termux USB] Streaming live telemetry...\n")
    print_monitor_header()

    # Trigger MCU stream in case it is in CLI idle mode
    time.sleep(0.1)
    dev.write(b"\r\nSTREAM ON\r\n")

    stats = StreamStats()
    rx_buf = ""

    try:
        while True:
            chunk = dev.read(size=4096, timeout_ms=500)
            if chunk:
                rx_buf += chunk.decode("utf-8", errors="ignore")
                while "\n" in rx_buf:
                    line, rx_buf = rx_buf.split("\n", 1)
                    line = line.strip()
                    if line:
                        sample = parse_telemetry_line(line)
                        if sample:
                            stats.record(sample)
                            print_sample_row(sample, stats.total_samples)
            else:
                time.sleep(0.01)
    except KeyboardInterrupt:
        pass
    finally:
        stats.print_summary()


def run_serial_monitor(port: str, baud: int):
    """Standard desktop/laptop serial monitor using pyserial."""
    try:
        import serial
    except ImportError:
        print("[Error] 'pyserial' package required for desktop serial. Run: pip install pyserial")
        sys.exit(1)

    print(f"\n[Desktop Serial] Opening {port} at {baud} baud...\n")
    ser = serial.Serial(port, baud, timeout=1.0)
    ser.dtr = True
    ser.rts = False # CRITICAL: RTS must be False for ESP32 run mode
    ser.reset_input_buffer()

    print_monitor_header()
    stats = StreamStats()

    try:
        while True:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if line:
                sample = parse_telemetry_line(line)
                if sample:
                    stats.record(sample)
                    print_sample_row(sample, stats.total_samples)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        stats.print_summary()


def print_monitor_header():
    print("=" * 105)
    print(f" {'#':<5} | {'TIME':<8} | {'NODE ID':<10} | {'RSSI':<9} | {'SIGNAL QUALITY':<24} | {'|B| (nT)':<10} | {'VBAT':<6} | {'STATUS'}")
    print("=" * 105)


def print_sample_row(sample: Dict[str, Any], count: int):
    now_str = time.strftime("%H:%M:%S")
    nid = sample["node_id"][:10]
    rssi = sample.get("rssi")
    rssi_str = f"{rssi:+4d} dBm" if (rssi is not None and rssi != 0) else "   --   "
    rssi_bar = format_rssi_bar(rssi, width=10)
    
    mag = math.sqrt(sample["x"]**2 + sample["y"]**2 + sample["z"]**2)
    vbat = sample.get("vbat")
    vbat_str = f"{vbat:.2f}V" if vbat is not None and vbat > 0 else "--"
    status_hex = sample.get("status_hex", "000000")

    print(f" {count:<5} | {now_str} | {nid:<10} | {rssi_str} | {rssi_bar:<24} | {mag:>9.1f} | {vbat_str:>6} | {status_hex}")


def main():
    parser = argparse.ArgumentParser(description="Live RSSI & Telemetry Monitor for Remote Magnetometers")
    parser.add_argument("pos_fd", nargs="?", type=str, default=None, help="Termux USB file descriptor (positional)")
    parser.add_argument("--port", type=str, default="/dev/ttyACM0", help="Serial port path (default: /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=921600, help="Baud rate (default: 921600)")
    parser.add_argument("--fd", type=int, default=None, help="Termux USB file descriptor")

    args = parser.parse_args()

    # Check for Termux USB File Descriptor
    termux_fd = None
    if os.getenv("TERMUX_USB_FD"):
        try:
            termux_fd = int(os.getenv("TERMUX_USB_FD"))
        except ValueError:
            pass
    elif args.fd is not None:
        termux_fd = args.fd
    elif args.pos_fd is not None and args.pos_fd.isdigit():
        termux_fd = int(args.pos_fd)

    if termux_fd is not None:
        run_termux_usb_monitor(termux_fd, args.baud)
    else:
        run_serial_monitor(args.port, args.baud)



if __name__ == "__main__":
    main()
