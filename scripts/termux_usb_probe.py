#!/usr/bin/env python3
"""
Termux USB Hardware Diagnostic & Raw Stream Probe (`termux_usb_probe.py`)

Diagnostics tool to verify physical USB connection, chip detection,
line control states (DTR/RTS), and raw incoming/outgoing serial data on Android.
"""

import sys
import os
import time
import argparse

sys.path.insert(0, os.path.dirname(__file__))
from termux_usb_driver import TermuxUsbDevice


def probe_device(fd: int, baudrate: int = 921600):
    print("=" * 80, flush=True)
    print("            TERMUX USB HARDWARE DIAGNOSTIC & PROBE", flush=True)
    print("=" * 80, flush=True)

    print(f"[1/4] Opening USB file descriptor {fd}...", flush=True)
    try:
        dev = TermuxUsbDevice(fd=fd, baudrate=baudrate)
    except Exception as e:
        print(f"[ERROR] Failed to initialize USB device on FD {fd}: {e}", flush=True)
        sys.exit(1)

    print(f"[2/4] Chipset:         {dev.chipset}", flush=True)
    print(f"      USB ID:          VID: 0x{dev.vid:04X} | PID: 0x{dev.pid:04X}", flush=True)
    print(f"      Interfaces:      {dev.interfaces}", flush=True)
    print(f"      Bulk IN EP:      0x{dev.in_ep:02X} ({dev.in_ep})", flush=True)
    print(f"      Bulk OUT EP:     0x{dev.out_ep:02X} ({dev.out_ep})", flush=True)
    print(f"[3/4] Line Config:     {baudrate} baud | DTR=1 (Active), RTS=0 (Normal Run)", flush=True)
    print("=" * 80, flush=True)
    print("[4/4] Sending 'STREAM ON' trigger to MCU...", flush=True)

    # Send trigger to start streaming
    time.sleep(0.1)
    dev.write(b"\r\nSTREAM ON\r\n")

    print("\n--- LIVE USB RAW STREAM (Press Ctrl+C to exit) ---\n", flush=True)

    total_bytes = 0
    start_time = time.time()

    try:
        while True:
            chunk = dev.read(size=4096, timeout_ms=300)
            if chunk:
                total_bytes += len(chunk)
                text = chunk.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()
            else:
                time.sleep(0.01)
    except KeyboardInterrupt:
        print("\n\n[INFO] Probe stopped by user.", flush=True)
    finally:
        elapsed = time.time() - start_time
        print("\n" + "=" * 80, flush=True)
        print(f"  Total Bytes Received: {total_bytes} bytes in {elapsed:.1f}s", flush=True)
        print("=" * 80 + "\n", flush=True)


def main():
    parser = argparse.ArgumentParser(description="Termux USB Hardware Diagnostic Probe")
    parser.add_argument("pos_fd", nargs="?", type=str, default=None, help="Termux USB file descriptor (positional)")
    parser.add_argument("--baud", type=int, default=921600, help="Baud rate (default: 921600)")
    parser.add_argument("--fd", type=int, default=None, help="Termux USB file descriptor")
    args = parser.parse_args()

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

    if termux_fd is None:
        print("[ERROR] No USB file descriptor provided. Launch via 'run_termux_probe.sh'.", flush=True)
        sys.exit(1)

    probe_device(termux_fd, args.baud)


if __name__ == "__main__":
    main()
