#!/usr/bin/env python3
"""
Simple Termux USB Serial Reader (`termux_serial_reader.py`)

A lightweight serial terminal monitor for Termux on Android.
Reads raw streaming ASCII data directly from the connected USB microcontroller.
"""

import sys
import os
import time
import argparse

sys.path.insert(0, os.path.dirname(__file__))
from termux_usb_driver import TermuxUsbDevice


def run_reader(fd: int, baud: int = 921600):
    print(f"=== Termux Serial Monitor (FD: {fd}, Baud: {baud}) ===", flush=True)

    try:
        dev = TermuxUsbDevice(fd=fd, baudrate=baud)
    except Exception as e:
        print(f"[Error] Failed to initialize USB device on FD {fd}: {e}", flush=True)
        sys.exit(1)

    print(f"Connected: {dev.chipset} on Bulk IN EP 0x{dev.in_ep:02X}", flush=True)
    print("--- Streaming output (Press Ctrl+C to stop) ---\n", flush=True)

    # Send wake/stream trigger
    time.sleep(0.1)
    dev.write(b"\r\nSTREAM ON\r\n")

    try:
        while True:
            chunk = dev.read(size=4096, timeout_ms=200)
            if chunk:
                text = chunk.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()
            else:
                time.sleep(0.01)
    except KeyboardInterrupt:
        print("\n\n[Stopped by user]", flush=True)


def main():
    parser = argparse.ArgumentParser(description="Simple Termux Serial Reader")
    parser.add_argument("pos_fd", nargs="?", type=str, default=None, help="Termux USB file descriptor")
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
        print("[Error] No USB file descriptor provided.", flush=True)
        sys.exit(1)

    run_reader(termux_fd, args.baud)


if __name__ == "__main__":
    main()
