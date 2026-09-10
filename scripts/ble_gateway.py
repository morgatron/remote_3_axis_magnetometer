#!/usr/bin/env python3
"""
BLE 1Mbps Connectionless Gateway Client (`scripts/ble_gateway.py`)

Listens for 1Mbps BLE Extended Advertising telemetry broadcast packets from the
ESP32 Multi-Protocol Receiver Gateway. Operates 100% connectionlessly without pairing,
connection handshakes, or supervision timeouts.

Prerequisites:
  pip install bleak requests

Usage:
  python3 scripts/ble_gateway.py
  python3 scripts/ble_gateway.py --max-samples 18
  python3 scripts/ble_gateway.py --forward-url http://localhost:8000/api/v1/telemetry
  python3 scripts/ble_gateway.py --csv field_session.csv
"""

import sys
import time
import math
import struct
import asyncio
import argparse
from typing import Optional

try:
    from bleak import BleakScanner
    from bleak.backends.device import BLEDevice
    from bleak.backends.scanner import AdvertisementData
except ImportError:
    print("Error: 'bleak' package is required. Install via: pip install bleak")
    sys.exit(1)

COMPANY_ID = 0xFFFF  # Custom / Testing Manufacturer Specific Data ID


class BleGatewayScanner:
    def __init__(self, target_name="MAG_GATEWAY", target_address=None, forward_url=None, csv_file=None, max_samples=None, timeout_s=None):
        self.target_name = target_name
        self.target_address = target_address
        self.forward_url = forward_url
        self.csv_file = csv_file
        self.max_samples = max_samples
        self.timeout_s = timeout_s
        self.csv_handle = None
        self.total_samples = 0
        self.start_time = time.time()
        self.magnitudes = []
        self.last_seen_seq = -1
        self.last_seen_ts = -1
        self._stop_event = asyncio.Event()

        if self.csv_file:
            self.csv_handle = open(self.csv_file, "a", encoding="utf-8")

        self.requests_session = None
        if self.forward_url:
            try:
                import requests
                self.requests_session = requests.Session()
            except ImportError:
                print("[WARNING] 'requests' package not found. Disabling HTTP forwarding.")

    def detection_callback(self, device: BLEDevice, adv_data: AdvertisementData):
        if self.target_address and device.address.upper() != self.target_address.upper():
            return

        if COMPANY_ID not in adv_data.manufacturer_data:
            return

        raw = adv_data.manufacturer_data[COMPANY_ID]
        if len(raw) < 27 or raw[:2] != b"MG":
            return

        seq = raw[2]
        timestamp_us = struct.unpack_from("<Q", raw, 11)[0]
        if seq == self.last_seen_seq and timestamp_us == self.last_seen_ts:
            return  # Duplicate broadcast of the same batch, ignore
        self.last_seen_seq = seq
        self.last_seen_ts = timestamp_us

        node_id = raw[3:11].split(b'\x00', 1)[0].decode('utf-8', errors='ignore')
        sample_interval_ms = struct.unpack_from("<H", raw, 19)[0]
        sample_count = raw[21]
        status = struct.unpack_from("<H", raw, 22)[0]
        vbat_mv = struct.unpack_from("<H", raw, 24)[0]
        rssi = struct.unpack_from("<b", raw, 26)[0]
        vbat = vbat_mv / 1000.0

        # Gateway battery voltage (offset 27 when 29-byte header is present)
        gw_vbat_mv = struct.unpack_from("<H", raw, 27)[0] if len(raw) >= 29 else 0
        gw_vbat = gw_vbat_mv / 1000.0 if gw_vbat_mv > 0 else 0.0
        header_size = 29 if len(raw) >= 29 else 27

        if sample_count == 0:
            # Heartbeat packet from gateway (no sensor samples)
            gw_disp_vbat = gw_vbat if gw_vbat > 0 else vbat
            gw_vbat_str = f"{gw_disp_vbat:.2f}V" if gw_disp_vbat > 0 else "--"
            print(f">>> [GATEWAY HEARTBEAT] ID: '{node_id}' | Gateway Battery: {gw_vbat_str} | Seq: {seq}")
            return

        gw_vbat_str = f" | GW Battery: {gw_vbat:.2f}V" if gw_vbat > 0 else ""
        print(f"\n>>> [GATEWAY RELAY] Node: '{node_id}' ({sample_count} samples, RSSI: {rssi} dBm{gw_vbat_str})")

        # Unpack each sample in the batch
        for i in range(sample_count):
            offset = header_size + i * 12
            if offset + 12 > len(raw):
                break
            x, y, z = struct.unpack_from("<fff", raw, offset)
            offset_from_newest_us = (sample_count - 1 - i) * sample_interval_ms * 1000
            sample_ts = timestamp_us - offset_from_newest_us if timestamp_us >= offset_from_newest_us else 0
            self.display_and_record_sample(node_id, sample_ts, x, y, z, f"{status:06X}", vbat, rssi)

        if self.max_samples and self.total_samples >= self.max_samples:
            self._stop_event.set()

    def display_and_record_sample(self, node_id, ts, x, y, z, status_hex, vbat, rssi):
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

        if self.requests_session and self.forward_url:
            try:
                payload = {
                    "node_id": node_id,
                    "timestamp_us": ts,
                    "x": x, "y": y, "z": z,
                    "status": int(status_hex, 16) if status_hex else 0,
                    "vbat": vbat,
                    "rssi": rssi,
                    "protocol": "BLE_EXT_ADV"
                }
                self.requests_session.post(self.forward_url, json=payload, timeout=0.5)
            except Exception:
                pass

    async def run(self):
        print("\n" + "=" * 92)
        print("     BLE 1Mbps CONNECTIONLESS EXTENDED ADVERTISING GATEWAY CLIENT")
        print("=" * 92)
        print(f"  Listening for:   '{self.target_name}'" + (f" ({self.target_address})" if self.target_address else " (Any Gateway)"))
        if self.forward_url:
            print(f"  Forward Server:  {self.forward_url}")
        if self.csv_file:
            print(f"  CSV Log File:    {self.csv_file}")
        print("=" * 92 + "\n")

        header_fmt = "{:>6} | {:<12} | {:>12} | {:>10} | {:>10} | {:>10} | {:>10} | {:>6} | {:>6} | {:<8}"
        print(header_fmt.format("SAMPLE", "NODE_ID", "TIMESTAMP_US", "Bx (nT)", "By (nT)", "Bz (nT)", "|B| (nT)", "VBAT", "RSSI", "STATUS"))
        print("-" * 92)

        scanner = BleakScanner(detection_callback=self.detection_callback)
        await scanner.start()
        print("[ACTIVE] Passive 1Mbps BLE Extended Advertising scanner listening...\n")

        try:
            while not self._stop_event.is_set():
                if self.timeout_s and (time.time() - self.start_time >= self.timeout_s):
                    print(f"\n[INFO] Reached timeout of {self.timeout_s}s. Stopping...")
                    break
                await asyncio.sleep(0.2)
        finally:
            await scanner.stop()
            if self.csv_handle:
                self.csv_handle.close()
            print("\n[INFO] Scanner stopped cleanly.")


def main():
    parser = argparse.ArgumentParser(
        description="BLE 1Mbps Connectionless Extended Advertising Gateway Client."
    )
    parser.add_argument("--name", type=str, default="MAG_GATEWAY", help="Gateway BLE device name")
    parser.add_argument("--address", type=str, help="Gateway BLE MAC / UUID address filter")
    parser.add_argument("--forward-url", type=str, help="HTTP URL to forward telemetry to Central Server")
    parser.add_argument("--csv", type=str, help="Save parsed telemetry to CSV file")
    parser.add_argument("--max-samples", type=int, help="Stop after receiving N samples")
    parser.add_argument("--timeout", type=float, help="Stop after N seconds")

    args = parser.parse_args()

    scanner = BleGatewayScanner(
        target_name=args.name,
        target_address=args.address,
        forward_url=args.forward_url,
        csv_file=args.csv,
        max_samples=args.max_samples,
        timeout_s=args.timeout
    )

    try:
        asyncio.run(scanner.run())
    except (KeyboardInterrupt, asyncio.CancelledError):
        print("\n[INFO] Exited cleanly.")


if __name__ == "__main__":
    main()
