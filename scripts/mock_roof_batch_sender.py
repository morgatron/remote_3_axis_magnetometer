#!/usr/bin/env python3
"""
Mock ROOF Gateway Batch Telemetry Sender (`scripts/mock_roof_batch_sender.py`)

Simulates the Heltec V4 ROOF receiver gateway transmitting HTTP POST batch telemetry
to the Central Data Server (`/api/v1/telemetry/batch`).

Mirrors the exact JSON serialization format and network behavior of `HttpBatchEgress.cpp`
and `WiFiManager.cpp`, enabling full end-to-end server verification without physical
ESP32 hardware.

Usage:
    # Single batch test against local dev server:
    python scripts/mock_roof_batch_sender.py --url http://localhost:8000 --once

    # Continuous 10-second field cadence simulation with verbose payload logging:
    python scripts/mock_roof_batch_sender.py --url http://localhost:8000 --verbose

    # Simulate 18-sample offline catch-up backlog burst with API Key:
    python scripts/mock_roof_batch_sender.py --url http://localhost:8000 --batch-size 18 --api-key secret123 --once
"""

import sys
import time
import math
import random
import argparse
from datetime import datetime, timezone
import requests

def format_iso_utc(dt: datetime) -> str:
    """Format datetime as YYYY-MM-DDTHH:MM:SS.mmmZ matching WiFiManager::getUtcIsoString."""
    ms = dt.microsecond // 1000
    return dt.strftime("%Y-%m-%dT%H:%M:%S") + f".{ms:03d}Z"

def generate_batch(node_id: str, count: int, sensor_model: str, sample_interval_sec: float, base_time: datetime, vbat_mv: int):
    """
    Constructs a JSON batch payload strictly matching HttpBatchEgress.cpp:
    - Top-level 'node_id'
    - Array of 'points' without redundant node_id keys
    - Realistic Earth magnetic field (Canberra ~23.4k nT X, -4.1k nT Y, 48.9k nT Z)
    """
    points = []
    
    # Starting offset for older samples in the batch
    start_time = base_time.timestamp() - ((count - 1) * sample_interval_sec)

    status_hex = "0x000000" if sensor_model.upper() == "FLC100" else "0xC00000"

    for i in range(count):
        sample_epoch = start_time + (i * sample_interval_sec)
        sample_dt = datetime.fromtimestamp(sample_epoch, tz=timezone.utc)
        iso_str = format_iso_utc(sample_dt)

        # Baseline Earth field + diurnal fluctuation + sensor noise
        noise_x = random.gauss(0, 0.4)
        noise_y = random.gauss(0, 0.4)
        noise_z = random.gauss(0, 0.5)

        x = round(23415.20 + 2.0 * math.sin(sample_epoch / 3600.0) + noise_x, 2)
        y = round(-4120.80 + 1.5 * math.cos(sample_epoch / 3600.0) + noise_y, 2)
        z = round(48910.10 + 3.0 * math.sin(sample_epoch / 7200.0) + noise_z, 2)

        temp_c = round(23.5 + random.uniform(-0.2, 0.2), 2)
        rssi_dbm = random.randint(-78, -70)

        point = {
            "timestamp": iso_str,
            "x": x,
            "y": y,
            "z": z,
            "temp": temp_c,
            "vbat": vbat_mv,
            "rssi": rssi_dbm,
            "status_flags": status_hex,
            "sensor_model": sensor_model.upper()
        }
        points.append(point)

    return {
        "node_id": node_id,
        "points": points
    }

def run_mock_sender(args):
    target_endpoint = args.url.rstrip("/") + "/api/v1/telemetry/batch"
    headers = {"Content-Type": "application/json"}
    if args.api_key:
        headers["X-API-Key"] = args.api_key

    print("================================================================")
    print("      MOCK ROOF RECEIVER -> CENTRAL SERVER BATCH SENDER        ")
    print("================================================================")
    print(f"Target Server:   {target_endpoint}")
    print(f"Sensor Node ID:  {args.node_id}")
    print(f"Sensor Model:    {args.sensor}")
    print(f"Batch Size:      {args.batch_size} samples / batch")
    print(f"Cadence:         {args.interval}s")
    print(f"API Key:         {'[CONFIGURED]' if args.api_key else '[NONE]'}")
    print("================================================================\n")

    batch_seq = 0
    sim_vbat = 4150  # Start at 4.15V

    try:
        while True:
            batch_seq += 1
            now = datetime.now(timezone.utc)
            payload = generate_batch(
                node_id=args.node_id,
                count=args.batch_size,
                sensor_model=args.sensor,
                sample_interval_sec=1.0,
                base_time=now,
                vbat_mv=sim_vbat
            )

            # Slowly simulate battery discharge (~1mV every 20 batches)
            if batch_seq % 20 == 0 and sim_vbat > 3300:
                sim_vbat -= 1

            t0 = time.time()
            try:
                resp = requests.post(target_endpoint, json=payload, headers=headers, timeout=args.timeout)
                duration_ms = (time.time() - t0) * 1000.0
                
                if resp.status_code == 201:
                    data = resp.json()
                    inserted = data.get("inserted", len(payload["points"]))
                    latest_p = payload["points"][-1]
                    print(f"[{datetime.now().strftime('%H:%M:%S')}] Batch #{batch_seq:04d} -> "
                          f"HTTP 201 Created ({duration_ms:.1f}ms) | "
                          f"Inserted {inserted} pts | "
                          f"Latest: |B|={math.sqrt(latest_p['x']**2 + latest_p['y']**2 + latest_p['z']**2):.1f} nT, "
                          f"Vbat={sim_vbat}mV, RSSI={latest_p['rssi']}dBm")
                else:
                    print(f"[{datetime.now().strftime('%H:%M:%S')}] Batch #{batch_seq:04d} -> "
                          f"HTTP ERROR {resp.status_code} ({duration_ms:.1f}ms): {resp.text.strip()}")
            except requests.exceptions.RequestException as e:
                print(f"[{datetime.now().strftime('%H:%M:%S')}] Batch #{batch_seq:04d} -> "
                      f"CONNECTION ERROR: {e}")

            if args.verbose:
                print(f"\n--- Payload Batch #{batch_seq} ---")
                import json
                print(json.dumps(payload, indent=2))
                print("---------------------------------\n")

            if args.once:
                print("\n[INFO] Single batch test complete (--once). Exiting.")
                break

            time.sleep(args.interval)

    except KeyboardInterrupt:
        print("\n[INFO] Mock sender stopped by user.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Simulate ROOF Heltec V4 receiver HTTP batch telemetry egress.")
    parser.add_argument("--url", type=str, default="http://localhost:8000", help="Central server base URL (default: http://localhost:8000)")
    parser.add_argument("--api-key", type=str, default=None, help="X-API-Key header value")
    parser.add_argument("--node-id", type=str, default="SPRINGBANK", help="Node ID (default: SPRINGBANK)")
    parser.add_argument("--sensor", type=str, default="FLC100", choices=["FLC100", "RM3100"], help="Sensor model (default: FLC100)")
    parser.add_argument("--batch-size", type=int, default=10, help="Samples per batch (default: 10, max 18)")
    parser.add_argument("--interval", type=float, default=10.0, help="Cadence in seconds between transmissions (default: 10.0)")
    parser.add_argument("--timeout", type=float, default=5.0, help="HTTP request timeout in seconds (default: 5.0)")
    parser.add_argument("--once", action="store_true", help="Send a single batch and exit")
    parser.add_argument("-v", "--verbose", action="store_true", help="Print full JSON payload on each send")

    args = parser.parse_args()
    if args.batch_size < 1 or args.batch_size > 18:
        print("Error: --batch-size must be between 1 and 18")
        sys.exit(1)

    run_mock_sender(args)
