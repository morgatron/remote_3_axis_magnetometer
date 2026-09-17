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
import json
import struct
import asyncio
import argparse
from datetime import datetime, timezone
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
            if not self.forward_url.startswith(("http://", "https://")):
                self.forward_url = "http://" + self.forward_url
            cleaned_url = self.forward_url.rstrip("/")
            if cleaned_url.endswith((":8000", ":8899", "localhost", "127.0.0.1")):
                self.forward_url = cleaned_url + "/api/v1/telemetry/batch"
            elif cleaned_url.endswith("/api/v1/telemetry") or cleaned_url.endswith("/api/telemetry"):
                self.forward_url = cleaned_url + "/batch"
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
        vbat = vbat_mv / 1000.0

        # Dynamically determine header size to maintain backward compatibility across firmware revisions:
        # Revision 3 (current, with thermistor temp_c_x100): 31 bytes
        # Revision 2 (with gw_vbat_mv): 29 bytes
        # Revision 1 (legacy): 27 bytes
        expected_samples_bytes = sample_count * 12
        raw_header_len = len(raw) - expected_samples_bytes if sample_count > 0 else len(raw)

        if raw_header_len >= 31:
            temp_c_x100 = struct.unpack_from("<h", raw, 26)[0]
            temp = (temp_c_x100 / 100.0) if temp_c_x100 != 0x7FFF else None
            rssi = struct.unpack_from("<b", raw, 28)[0]
            gw_vbat_mv = struct.unpack_from("<H", raw, 29)[0]
            header_size = 31
        elif raw_header_len >= 29:
            temp = None
            rssi = struct.unpack_from("<b", raw, 26)[0]
            gw_vbat_mv = struct.unpack_from("<H", raw, 27)[0]
            header_size = 29
        else:
            temp = None
            rssi = struct.unpack_from("<b", raw, 26)[0]
            gw_vbat_mv = 0
            header_size = 27

        gw_vbat = gw_vbat_mv / 1000.0 if gw_vbat_mv > 0 else 0.0

        if sample_count == 0:
            # Heartbeat packet from gateway (no sensor samples)
            gw_disp_vbat = gw_vbat if gw_vbat > 0 else vbat
            gw_vbat_str = f"{gw_disp_vbat:.2f}V" if gw_disp_vbat > 0 else "--"
            print(f">>> [GATEWAY HEARTBEAT] ID: '{node_id}' | Gateway Battery: {gw_vbat_str} | Seq: {seq}")
            return

        is_mock = bool(status & 0x8000)
        mock_tag = " [MOCK DATA]" if is_mock else ""
        gw_vbat_str = f" | GW Battery: {gw_vbat:.2f}V" if gw_vbat > 0 else ""
        temp_str = f" | Temp: {temp:.1f}°C" if temp is not None else ""
        print(f"\n>>> [GATEWAY RELAY] Node: '{node_id}'{mock_tag} ({sample_count} samples, RSSI: {rssi} dBm{gw_vbat_str}{temp_str})")

        status_disp = "MOCK" if is_mock else f"{status:04X}"
        batch_points = []
        arrival_wall_time = time.time()

        # Unpack each sample in the batch
        for i in range(sample_count):
            offset = header_size + i * 12
            if offset + 12 > len(raw):
                break
            x, y, z = struct.unpack_from("<fff", raw, offset)
            offset_from_newest_sec = ((sample_count - 1 - i) * sample_interval_ms) / 1000.0
            sample_time_sec = arrival_wall_time - offset_from_newest_sec
            iso_ts = datetime.fromtimestamp(sample_time_sec, tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

            offset_from_newest_us = (sample_count - 1 - i) * sample_interval_ms * 1000
            sample_ts = timestamp_us - offset_from_newest_us if timestamp_us >= offset_from_newest_us else 0
            self.display_and_record_sample(node_id, sample_ts, x, y, z, status_disp, vbat, rssi, temp)

            batch_points.append({
                "node_id": node_id,
                "timestamp": iso_ts,
                "x": x, "y": y, "z": z,
                "units": "nT",
                "temp": temp,
                "status_flags": f"0x{status:06X}",
                "vbat": vbat_mv,
                "rssi": rssi,
                "extra_json": json.dumps({"gw_id": self.target_name, "gw_vbat_mv": gw_vbat_mv}) if gw_vbat_mv > 0 else None
            })

        # Batch forward to Central Server
        if self.requests_session and self.forward_url and batch_points:
            try:
                if "/batch" in self.forward_url:
                    payload = {"node_id": node_id, "points": batch_points}
                    resp = self.requests_session.post(self.forward_url, json=payload, timeout=2.0)
                else:
                    for pt in batch_points:
                        resp = self.requests_session.post(self.forward_url, json=pt, timeout=1.0)
                if resp.status_code not in (200, 201):
                    print(f"  [FORWARD WARNING] HTTP {resp.status_code}: {resp.text}")
            except Exception as e:
                print(f"  [FORWARD ERROR] Failed to forward telemetry: {e}")

        if self.max_samples and self.total_samples >= self.max_samples:
            self._stop_event.set()

    def display_and_record_sample(self, node_id, ts, x, y, z, status_hex, vbat, rssi, temp=None):
        self.total_samples += 1
        mag = math.sqrt(x*x + y*y + z*z)
        self.magnitudes.append(mag)

        vbat_str = f"{vbat:.2f}V" if vbat > 0 else "--"
        rssi_str = f"{rssi}dBm" if rssi != 0 else "--"
        temp_str = f"{temp:.1f}C" if temp is not None else "--"

        row_fmt = "{:>6} | {:<12} | {:>12} | {:>10.2f} | {:>10.2f} | {:>10.2f} | {:>10.2f} | {:>6} | {:>6} | {:>6} | {:<8}"
        print(row_fmt.format(
            self.total_samples,
            node_id[:12],
            ts,
            x, y, z, mag,
            temp_str,
            vbat_str,
            rssi_str,
            status_hex
        ))

        if self.csv_handle:
            temp_val = f"{temp:.2f}" if temp is not None else ""
            self.csv_handle.write(f"{node_id},{ts},{x:.2f},{y:.2f},{z:.2f},{mag:.2f},{status_hex},{temp_val},{vbat:.2f},{rssi}\n")
            self.csv_handle.flush()

    async def run(self):
        print("\n" + "=" * 102)
        print("     BLE 1Mbps CONNECTIONLESS EXTENDED ADVERTISING GATEWAY CLIENT")
        print("=" * 102)
        print(f"  Listening for:   '{self.target_name}'" + (f" ({self.target_address})" if self.target_address else " (Any Gateway)"))
        if self.forward_url:
            print(f"  Forward Server:  {self.forward_url}")
        if self.csv_file:
            print(f"  CSV Log File:    {self.csv_file}")
        print("=" * 102 + "\n")

        header_fmt = "{:>6} | {:<12} | {:>12} | {:>10} | {:>10} | {:>10} | {:>10} | {:>6} | {:>6} | {:>6} | {:<8}"
        print(header_fmt.format("SAMPLE", "NODE_ID", "TIMESTAMP_US", "Bx (nT)", "By (nT)", "Bz (nT)", "|B| (nT)", "TEMP", "VBAT", "RSSI", "STATUS"))
        print("-" * 102)

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
