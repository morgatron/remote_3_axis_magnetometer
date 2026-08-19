"""
Stream Parser Helper (`stream_parser.py`)

Shared CSV line parser for MCU telemetry streams.
Used by desktop_app (serial_worker.py, udp_worker.py) and central_service (gateway.py).
"""

import time
from typing import Optional, Dict, Any

def parse_telemetry_line(line: str) -> Optional[Dict[str, Any]]:
    """
    Parses standard MCU telemetry CSV line:
    Format: device_id,timestamp_us,x_nT,y_nT,z_nT,status_hex
    Example: SENSOR_01,123456789,23415.20,-4120.80,48910.10,C00000

    Returns dict with keys: node_id, timestamp_us, timestamp_iso, x, y, z, status_hex, status_int
    Or None if line is a status/log line or invalid CSV.
    """
    if not line:
        return None

    # Ignore text status/log messages from MCU
    line_str = line.strip()
    line_up = line_str.upper()
    if any(kw in line_up for kw in ["SENSOR:", "RM3100", "FLC100", "RATE CODE:", "STATUS", "REVID", "DEVICE ID:"]):
        return None

    parts = line_str.split(",")
    try:
        if len(parts) >= 6:
            device_id = parts[0].strip()
            ts_us = float(parts[1])
            x = float(parts[2])
            y = float(parts[3])
            z = float(parts[4])
            clean_status = parts[5].strip().split()[0]
            status_int = int(clean_status, 16)

            temp = float(parts[6]) if len(parts) >= 7 and parts[6].strip() else None
            vbat = int(float(parts[7])) if len(parts) >= 8 and parts[7].strip() else None
            rssi = int(float(parts[8])) if len(parts) >= 9 and parts[8].strip() else None

            return {
                "node_id": device_id,
                "timestamp_us": ts_us,
                "timestamp_iso": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "x": x,
                "y": y,
                "z": z,
                "status_hex": clean_status,
                "status_int": status_int,
                "temp": temp,
                "vbat": vbat,
                "rssi": rssi
            }
        elif len(parts) == 5:
            device_id = "LOCAL_NODE"
            ts_us = float(parts[0])
            x = float(parts[1])
            y = float(parts[2])
            z = float(parts[3])
            clean_status = parts[4].strip().split()[0]
            status_int = int(clean_status, 16)

            return {
                "node_id": device_id,
                "timestamp_us": ts_us,
                "timestamp_iso": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "x": x,
                "y": y,
                "z": z,
                "status_hex": clean_status,
                "status_int": status_int
            }
    except (ValueError, IndexError):
        pass

    return None


def parse_telemetry_batch(raw_payload: str, arrival_wall_time: Optional[float] = None) -> list:
    """
    Parses a multi-line CSV payload buffer (e.g. UDP packet or batch queue flush).
    Applies relative microsecond delta-t back-calculation anchored to arrival_wall_time
    to ensure perfect historical sample spacing across network outages.
    """
    from datetime import datetime, timezone

    if arrival_wall_time is None:
        arrival_wall_time = time.time()

    lines = [l.strip() for l in raw_payload.splitlines() if l.strip()]
    parsed_samples = [parse_telemetry_line(l) for l in lines]
    parsed_samples = [s for s in parsed_samples if s is not None]

    if not parsed_samples:
        return []

    # Anchor to the latest sample's MCU microsecond uptime in the batch
    latest_us = parsed_samples[-1]["timestamp_us"]

    for s in parsed_samples:
        delta_sec = (latest_us - s["timestamp_us"]) / 1_000_000.0
        sample_utc = arrival_wall_time - delta_sec
        s["timestamp_iso"] = datetime.fromtimestamp(sample_utc, tz=timezone.utc).isoformat()

    return parsed_samples

