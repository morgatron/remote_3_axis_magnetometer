#!/usr/bin/env python3
"""
BLE 1Mbps Connectionless Telemetry & Diagnostic Monitor (`scripts/ble_monitor.py`)

Listens for 1Mbps BLE Extended Advertising telemetry broadcast packets from the
ESP32 Multi-Protocol Receiver Gateway, or reads decoded CSV from an ESP32 BLE-to-serial bridge.
Operates 100% connectionlessly without pairing, connection handshakes, or supervision timeouts.

Prerequisites:
  pip install bleak requests pyserial

Usage:
  python3 scripts/ble_monitor.py
  python3 scripts/ble_monitor.py --serial /dev/ttyACM2
  python3 scripts/ble_monitor.py --max-samples 18
  python3 scripts/ble_monitor.py --forward-url http://localhost:8000/api/v1/telemetry
  python3 scripts/ble_monitor.py --csv field_session.csv
"""

import sys
import time
import math
import json
import struct
import asyncio
import argparse
import os
from datetime import datetime, timezone
from typing import Optional

# Import shared NodeEpochTracker from central_service or fallback definition
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "central_service")))
try:
    from stream_parser import NodeEpochTracker
except ImportError:
    class NodeEpochTracker:
        def __init__(self, drift_alpha=0.02, max_live_latency_sec=2.5):
            self.drift_alpha = drift_alpha
            self.max_live_latency_sec = max_live_latency_sec
            self._nodes = {}
        def get_sample_utc(self, node_id, ts_us, arrival_wall_time=None):
            if arrival_wall_time is None: arrival_wall_time = time.time()
            ts_sec = ts_us / 1_000_000.0
            info = self._nodes.get(node_id)
            if info is None or ts_sec < info["last_ts_sec"] - 5.0:
                epoch = arrival_wall_time - ts_sec
                self._nodes[node_id] = {"epoch": epoch, "last_ts_sec": ts_sec}
                return arrival_wall_time
            epoch = info["epoch"]
            sample_utc = epoch + ts_sec
            latency = arrival_wall_time - sample_utc
            if -0.5 <= latency <= self.max_live_latency_sec:
                info["epoch"] = (1.0 - self.drift_alpha) * epoch + self.drift_alpha * (arrival_wall_time - ts_sec)
            if ts_sec > info["last_ts_sec"]: info["last_ts_sec"] = ts_sec
            return sample_utc
        def get_sample_iso(self, node_id, ts_us, arrival_wall_time=None):
            return datetime.fromtimestamp(self.get_sample_utc(node_id, ts_us, arrival_wall_time), tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

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
        self.epoch_tracker = NodeEpochTracker()
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
            # Heartbeat / Diagnostic packet from gateway (no sensor samples)
            gw_disp_vbat = gw_vbat if gw_vbat > 0 else vbat
            gw_vbat_str = f"{gw_disp_vbat:.2f}V" if gw_disp_vbat > 0 else "--"

            diag_event = (status >> 12) & 0x0F
            sched_state = (status >> 8) & 0x0F
            misses = status & 0xFF
            metric = sample_interval_ms

            state_names = ["DISCOVERY", "SLEEPING", "LISTENING", "PERIODIC_LOOKOUT"]
            event_names = ["IDLE_HEARTBEAT", "WINDOW_MISSED", "LOST_SYNC_DISCOVERY", "PERIODIC_LOOKOUT", "SYNC_ACQUIRED"]

            state_str = state_names[sched_state] if sched_state < len(state_names) else f"STATE_{sched_state}"
            event_str = event_names[diag_event] if diag_event < len(event_names) else f"EVENT_{diag_event}"

            if diag_event == 0:
                print(f">>> [GATEWAY HEARTBEAT] ID: '{node_id}' | State: {state_str} | Gateway Battery: {gw_vbat_str} | Seq: {seq}")
            elif diag_event in (1, 2):
                print(f"\n>>> [GATEWAY ALERT] Event: {event_str} | State: {state_str} | Misses: {misses} | Target: '{node_id}' | On-Time: {metric}ms | Batt: {gw_vbat_str}")
            elif diag_event == 3:
                print(f"\n>>> [GATEWAY NOTICE] Event: PERIODIC_LOOKOUT (10-min scan) | State: {state_str} | Batt: {gw_vbat_str}")
            elif diag_event == 4:
                print(f"\n>>> [GATEWAY STATUS] Event: SYNC_ACQUIRED | State: {state_str} | Target: '{node_id}' | Disc Duration: {metric}ms | Batt: {gw_vbat_str}")
            else:
                print(f">>> [GATEWAY DIAG] Event: {event_str} | State: {state_str} | Misses: {misses} | Battery: {gw_vbat_str} | Seq: {seq}")
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
            offset_from_newest_us = (sample_count - 1 - i) * sample_interval_ms * 1000
            sample_ts = timestamp_us - offset_from_newest_us if timestamp_us >= offset_from_newest_us else 0
            iso_ts = self.epoch_tracker.get_sample_iso(node_id, sample_ts, arrival_wall_time)
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

    def run_serial(self, port: str, baudrate: int = 921600):
        import serial
        print("\n" + "=" * 92)
        print("     BLE-TO-SERIAL BRIDGE TELEMETRY GATEWAY CLIENT")
        print("=" * 92)
        print(f"  Serial Device:   {port} ({baudrate} baud)")
        if self.forward_url:
            print(f"  Forward Server:  {self.forward_url}")
        if self.csv_file:
            print(f"  CSV Log File:    {self.csv_file}")
        print("=" * 92 + "\n")

        header_fmt = "{:>6} | {:<12} | {:>12} | {:>10} | {:>10} | {:>10} | {:>10} | {:>6} | {:>6} | {:<8}"
        print(header_fmt.format("SAMPLE", "NODE_ID", "TIMESTAMP_US", "Bx (nT)", "By (nT)", "Bz (nT)", "|B| (nT)", "VBAT", "RSSI", "STATUS"))
        print("-" * 92)

        ser = serial.Serial(port, baudrate, timeout=0.2)
        ser.dtr = True
        ser.rts = False
        print(f"[ACTIVE] Listening for decoded BLE telemetry on {port}...\n")

        try:
            while not self._stop_event.is_set():
                if self.timeout_s and (time.time() - self.start_time >= self.timeout_s):
                    print(f"\n[INFO] Reached timeout of {self.timeout_s}s. Stopping...")
                    break

                raw_line = ser.readline().decode("utf-8", errors="ignore").strip()
                if not raw_line:
                    continue

                if raw_line.startswith("#") or raw_line.startswith("="):
                    # Status or heartbeat line from bridge
                    print(f"  {raw_line}")
                    continue

                parts = raw_line.split(",")
                if len(parts) >= 6:
                    node_id = parts[0].strip()
                    try:
                        ts = int(float(parts[1]))
                        x = float(parts[2])
                        y = float(parts[3])
                        z = float(parts[4])
                        status_hex = parts[5].strip()
                        vbat = float(parts[7]) if len(parts) >= 8 and parts[7].strip() else 0.0
                        rssi = int(float(parts[8])) if len(parts) >= 9 and parts[8].strip() else 0
                    except (ValueError, IndexError):
                        continue

                    self.display_and_record_sample(node_id, ts, x, y, z, status_hex, vbat, rssi)

                    # HTTP Forwarding
                    if self.requests_session and self.forward_url:
                        pt = {
                            "node_id": node_id,
                            "timestamp": datetime.fromtimestamp(time.time(), tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
                            "x": x, "y": y, "z": z,
                            "units": "nT",
                            "status_flags": f"0x{status_hex}",
                            "vbat": int(vbat * 1000.0),
                            "rssi": rssi,
                            "extra_json": None
                        }
                        try:
                            if "/batch" in self.forward_url:
                                self.requests_session.post(self.forward_url, json={"node_id": node_id, "points": [pt]}, timeout=1.0)
                            else:
                                self.requests_session.post(self.forward_url, json=pt, timeout=1.0)
                        except Exception as e:
                            print(f"  [FORWARD ERROR] {e}")

                    if self.max_samples and self.total_samples >= self.max_samples:
                        break
        finally:
            ser.close()
            if self.csv_handle:
                self.csv_handle.close()
            print("\n[INFO] Serial listener stopped cleanly.")


def main():
    parser = argparse.ArgumentParser(
        description="BLE 1Mbps Connectionless Extended Advertising Gateway Client."
    )
    parser.add_argument("--name", type=str, default="MAG_GATEWAY", help="Gateway BLE device name")
    parser.add_argument("--address", type=str, help="Gateway BLE MAC / UUID address filter")
    parser.add_argument("--serial", type=str, help="Serial port for hardware BLE bridge (e.g. /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=921600, help="Baud rate for serial bridge (default: 921600)")
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
        if args.serial:
            scanner.run_serial(args.serial, baudrate=args.baud)
        else:
            asyncio.run(scanner.run())
    except (KeyboardInterrupt, asyncio.CancelledError):
        print("\n[INFO] Exited cleanly.")


if __name__ == "__main__":
    main()
