#!/usr/bin/env python3
"""
BLE 1Mbps GATT Gateway Client (`scripts/ble_gateway.py`)

Connects to the ESP32 / nRF52840 Multi-Protocol Receiver Gateway over standard 1 Mbps BLE,
receives real-time telemetry relayed from remote field sensor nodes (via Coded PHY or LoRa),
displays live magnetic field vector magnitudes, and optionally forwards data to the Central Server.

Prerequisites:
  pip install bleak requests

Usage:
  python3 scripts/ble_gateway.py
  python3 scripts/ble_gateway.py --name MAG_GATEWAY --forward-url http://localhost:8000/api/v1/telemetry
  python3 scripts/ble_gateway.py --csv field_session.csv
"""

import sys
import time
import math
import asyncio
import argparse
from typing import Optional

try:
    from bleak import BleakClient, BleakScanner
    from bleak.backends.device import BLEDevice
    from bleak.backends.characteristic import BleakGATTCharacteristic
except ImportError:
    print("Error: 'bleak' package is required. Install via: pip install bleak")
    sys.exit(1)

# Nordic UART Service (NUS) UUIDs
NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_RX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"


class BleGatewayClient:
    def __init__(self, target_name="MAG_GATEWAY", target_address=None, forward_url=None, csv_file=None):
        self.target_name = target_name
        self.target_address = target_address
        self.forward_url = forward_url
        self.csv_file = csv_file
        self.csv_handle = None
        self.total_samples = 0
        self.start_time = time.time()
        self.magnitudes = []
        self._buffer = ""

        if self.csv_file:
            self.csv_handle = open(self.csv_file, "a", encoding="utf-8")

        # Forwarder session
        self.requests_session = None
        if self.forward_url:
            try:
                import requests
                self.requests_session = requests.Session()
            except ImportError:
                print("[WARNING] 'requests' package not found. Disabling HTTP forwarding.")

    def parse_and_display_line(self, line: str):
        line = line.strip()
        if not line or line.startswith("=") or line.startswith("FIRMWARE") or line.startswith("---") or line.startswith("HELP"):
            return

        parts = line.split(',')
        if len(parts) < 5:
            return

        node_id = "LOCAL"
        ts = 0
        x, y, z = 0.0, 0.0, 0.0
        status_hex = "000000"
        vbat = 0.0
        rssi = 0

        try:
            if len(parts) >= 9:
                node_id = parts[0].strip()
                ts = int(parts[1])
                x = float(parts[2])
                y = float(parts[3])
                z = float(parts[4])
                status_hex = parts[5].strip()
                vbat = float(parts[7])
                rssi = int(parts[8])
            elif len(parts) >= 6:
                node_id = parts[0].strip()
                ts = int(parts[1])
                x = float(parts[2])
                y = float(parts[3])
                z = float(parts[4])
                status_hex = parts[5].strip()
            elif len(parts) == 5:
                ts = int(parts[0])
                x = float(parts[1])
                y = float(parts[2])
                z = float(parts[3])
                status_hex = parts[4].strip()
        except ValueError:
            return

        self.total_samples += 1
        mag = math.sqrt(x*x + y*y + z*z)
        self.magnitudes.append(mag)

        vbat_str = f"{vbat:.2f}V" if vbat > 0 else "--"
        rssi_str = f"{rssi}dBm" if rssi != 0 else "--"

        row_fmt = "{:>6} | {:<12} | {:>12} | {:>10.2f} | {:>10.2f} | {:>10.2f} | {:>10.2f} | {:>6} | {:>6} | {:<8}"
        print(row_fmt.format(
            self.total_samples,
            node_id[:12],
            ts,
            x, y, z, mag,
            vbat_str,
            rssi_str,
            status_hex
        ))

        if self.csv_handle:
            self.csv_handle.write(f"{node_id},{ts},{x:.2f},{y:.2f},{z:.2f},{mag:.2f},{status_hex},{vbat:.2f},{rssi}\n")
            self.csv_handle.flush()

        # HTTP forwarding to Central Server if configured
        if self.requests_session and self.forward_url:
            try:
                payload = {
                    "node_id": node_id,
                    "timestamp_us": ts,
                    "x": x, "y": y, "z": z,
                    "status": int(status_hex, 16) if status_hex else 0,
                    "vbat": vbat,
                    "rssi": rssi,
                    "protocol": "BLE_RELAY"
                }
                self.requests_session.post(self.forward_url, json=payload, timeout=0.5)
            except Exception:
                pass

    def notification_handler(self, characteristic: BleakGATTCharacteristic, data: bytearray):
        chunk = data.decode("utf-8", errors="ignore")
        self._buffer += chunk
        while "\n" in self._buffer:
            line, self._buffer = self._buffer.split("\n", 1)
            self.parse_and_display_line(line)

    async def run(self):
        print("\n" + "=" * 92)
        print("            BLE 1Mbps RELAY GATEWAY CLIENT FOR MAGNETOMETER NODES")
        print("=" * 92)
        print(f"  Target Device:   '{self.target_name}'" + (f" ({self.target_address})" if self.target_address else ""))
        if self.forward_url:
            print(f"  Forward Server:  {self.forward_url}")
        if self.csv_file:
            print(f"  CSV Log File:    {self.csv_file}")
        print("=" * 92 + "\n")

        while True:
            try:
                print(f"Scanning for BLE Receiver Gateway '{self.target_name}'...")
                device: Optional[BLEDevice] = None

                if self.target_address:
                    device = await BleakScanner.find_device_by_address(self.target_address, timeout=10.0)
                else:
                    devices = await BleakScanner.discover(timeout=5.0)
                    for d in devices:
                        if d.name and (self.target_name.upper() in d.name.upper() or "MAG_GATEWAY" in d.name.upper()):
                            device = d
                            break

                if not device:
                    print(f"[RETRY] Receiver gateway '{self.target_name}' not found. Retrying in 3s...")
                    await asyncio.sleep(3)
                    continue

                print(f"[CONNECTED] Found gateway: {device.name} ({device.address}). Connecting...")

                async with BleakClient(device) as client:
                    if not client.is_connected:
                        print("[ERROR] Connection handshake failed. Retrying...")
                        await asyncio.sleep(2)
                        continue

                    print(f"[ACTIVE] Connected to {device.name} over standard 1 Mbps BLE!")
                    print("-" * 92)
                    header_fmt = "{:>6} | {:<12} | {:>12} | {:>10} | {:>10} | {:>10} | {:>10} | {:>6} | {:>6} | {:<8}"
                    print(header_fmt.format("SAMPLE", "NODE_ID", "TIMESTAMP_US", "Bx (nT)", "By (nT)", "Bz (nT)", "|B| (nT)", "VBAT", "RSSI", "STATUS"))
                    print("-" * 92)

                    await client.start_notify(NUS_TX_CHAR_UUID, self.notification_handler)

                    while client.is_connected:
                        await asyncio.sleep(1)

            except asyncio.CancelledError:
                print("\n[INFO] Gateway client stopped by user.")
                break
            except Exception as e:
                print(f"\n[DISCONNECTED] BLE connection dropped: {e}. Reconnecting in 3s...")
                await asyncio.sleep(3)

        if self.csv_handle:
            self.csv_handle.close()


def main():
    parser = argparse.ArgumentParser(
        description="BLE 1Mbps GATT Gateway Client for Remote Magnetometer Nodes."
    )
    parser.add_argument("--name", type=str, default="MAG_GATEWAY", help="Gateway BLE device name (default: MAG_GATEWAY)")
    parser.add_argument("--address", type=str, help="Gateway BLE MAC / UUID address")
    parser.add_argument("--forward-url", type=str, help="HTTP URL to forward telemetry to Central Server")
    parser.add_argument("--csv", type=str, help="Save parsed telemetry to CSV file")

    args = parser.parse_args()

    client = BleGatewayClient(
        target_name=args.name,
        target_address=args.address,
        forward_url=args.forward_url,
        csv_file=args.csv
    )

    try:
        asyncio.run(client.run())
    except KeyboardInterrupt:
        print("\n[INFO] Exited.")


if __name__ == "__main__":
    main()
