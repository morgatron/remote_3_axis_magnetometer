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

# Ensure unbuffered stdout
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(line_buffering=True)

sys.path.insert(0, os.path.dirname(__file__))
from termux_usb_driver import TermuxUsbDevice


def probe_device(fd: int, baudrate: int = 921600):
    print("=" * 80)
    print("            TERMUX USB HARDWARE DIAGNOSTIC & PROBE")
    print("=" * 80)

    try:
        dev = TermuxUsbDevice(fd=fd, baudrate=baudrate)
    except Exception as e:
        print(f"[ERROR] Failed to initialize USB device on FD {fd}: {e}")
        sys.exit(1)

    print(f"  Chipset:         {dev.chipset}")
    print(f"  USB ID:          VID: 0x{dev.vid:04X} | PID: 0x{dev.pid:04X}")
    print(f"  Interfaces:      {dev.interfaces}")
    print(f"  Bulk IN EP:      0x{dev.in_ep:02X} ({dev.in_ep})")
    print(f"  Bulk OUT EP:     0x{dev.out_ep:02X} ({dev.out_ep})")
    print(f"  Baud Rate:       {baudrate}")
    print(f"  DTR / RTS State: DTR=1 (Active), RTS=0 (Normal Run / No Reset)")
    print("=" * 80)
    print("Listening for incoming bytes... (Press Ctrl+C to exit)\n")

    # Send a newline and 'STREAM ON' in case the MCU CLI is awaiting trigger
    time.sleep(0.2)
    dev.write(b"\r\nSTREAM ON\r\n")

    total_bytes = 0
    start_time = time.time()

    try:
        while True:
            chunk = dev.read(size=4096, timeout_ms=500)
            if chunk:
                total_bytes += len(chunk)
                text = chunk.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()
            else:
                time.sleep(0.02)
    except KeyboardInterrupt:
        print("\n\n[INFO] Probe stopped by user.")
    finally:
        elapsed = time.time() - start_time
        print("\n" + "=" * 80)
        print(f"  Total Bytes Received: {total_bytes} bytes in {elapsed:.1f}s")
        print("=" * 80 + "\n")


def main():
    parser = argparse.ArgumentParser(description="Termux USB Hardware Diagnostic Probe")
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
    elif len(sys.argv) > 1 and sys.argv[1].isdigit():
        termux_fd = int(sys.argv[1])

    if termux_fd is None:
        print("[ERROR] No USB file descriptor provided. Launch via 'run_termux_probe.sh'.")
        sys.exit(1)

    probe_device(termux_fd, args.baud)


if __name__ == "__main__":
    main()
