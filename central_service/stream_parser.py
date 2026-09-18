"""
Stream Parser Helper (`stream_parser.py`)

Shared CSV line parser and epoch tracker for MCU telemetry streams.
Used by desktop_app (serial_worker.py, udp_worker.py) and central_service (gateway.py).
"""

import time
import math
from datetime import datetime, timezone
from typing import Optional, Dict, Any


class NodeEpochTracker:
    """
    Maintains a rolling monotonic boot epoch anchor (T_epoch = T_wall - ts_sec) per node.
    Enables accurate historical UTC timestamp reconstruction for backlog samples,
    immune to network dropouts, transmission delays, and microcontroller crystal frequency drift.
    """
    def __init__(self, drift_alpha: float = 0.02, max_live_latency_sec: float = 2.5):
        self.drift_alpha = drift_alpha
        self.max_live_latency_sec = max_live_latency_sec
        # node_id -> {"epoch": float, "last_ts_sec": float, "last_update": float}
        self._nodes: Dict[str, Dict[str, float]] = {}

    def get_sample_utc(self, node_id: str, ts_us: float, arrival_wall_time: Optional[float] = None) -> float:
        if arrival_wall_time is None:
            arrival_wall_time = time.time()
        ts_sec = ts_us / 1_000_000.0

        info = self._nodes.get(node_id)
        if info is None:
            # First observation of node: establish initial epoch anchor
            epoch = arrival_wall_time - ts_sec
            self._nodes[node_id] = {
                "epoch": epoch,
                "last_ts_sec": ts_sec,
                "last_update": arrival_wall_time
            }
            return arrival_wall_time

        epoch = info["epoch"]
        last_ts_sec = info["last_ts_sec"]

        # Check for node reboot or hardware timer rollover:
        # If ts_sec jumps backwards significantly (> 5.0s backwards), node has rebooted.
        if ts_sec < last_ts_sec - 5.0:
            epoch = arrival_wall_time - ts_sec
            info["epoch"] = epoch
            info["last_ts_sec"] = ts_sec
            info["last_update"] = arrival_wall_time
            return arrival_wall_time

        # Calculate absolute sample UTC based on current locked epoch
        sample_utc = epoch + ts_sec
        latency = arrival_wall_time - sample_utc

        # If latency is within normal live stream window (-0.5s to max_live_latency_sec):
        # Gently adjust the locked epoch using EMA to track host NTP and device crystal drift (~15 ppm)
        if -0.5 <= latency <= self.max_live_latency_sec:
            measured_epoch = arrival_wall_time - ts_sec
            info["epoch"] = (1.0 - self.drift_alpha) * epoch + self.drift_alpha * measured_epoch
            info["last_update"] = arrival_wall_time

        if ts_sec > info["last_ts_sec"]:
            info["last_ts_sec"] = ts_sec

        return sample_utc

    def get_sample_iso(self, node_id: str, ts_us: float, arrival_wall_time: Optional[float] = None) -> str:
        utc = self.get_sample_utc(node_id, ts_us, arrival_wall_time)
        return datetime.fromtimestamp(utc, tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    def has_node(self, node_id: str) -> bool:
        return node_id in self._nodes

    def reset_node(self, node_id: str):
        self._nodes.pop(node_id, None)

    def clear(self):
        self._nodes.clear()


def parse_telemetry_line(
    line: str,
    arrival_wall_time: Optional[float] = None,
    epoch_tracker: Optional[NodeEpochTracker] = None
) -> Optional[Dict[str, Any]]:
    """
    Parses standard MCU telemetry CSV line:
    Format: device_id,timestamp_us,x_nT,y_nT,z_nT,status_hex[,temp,vbat,rssi,gw_vbat]
    Example: SENSOR_01,123456789,23415.20,-4120.80,48910.10,C00000,24.5,3.75,-54,4.12

    Returns dict with keys: node_id, timestamp_us, timestamp_iso, x, y, z, status_hex, status_int, temp, vbat, rssi, gw_vbat
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
            if math.isnan(x) or math.isinf(x) or math.isnan(y) or math.isinf(y) or math.isnan(z) or math.isinf(z):
                return None

            clean_status = parts[5].strip().split()[0]
            status_int = int(clean_status, 16)

            temp = float(parts[6]) if len(parts) >= 7 and parts[6].strip() else None
            if temp is not None and (math.isnan(temp) or math.isinf(temp)):
                temp = None

            vbat = float(parts[7]) if len(parts) >= 8 and parts[7].strip() else None
            if vbat is not None and (math.isnan(vbat) or math.isinf(vbat)):
                vbat = None

            rssi = int(float(parts[8])) if len(parts) >= 9 and parts[8].strip() else None

            gw_vbat = float(parts[9]) if len(parts) >= 10 and parts[9].strip() else None
            if gw_vbat is not None and (math.isnan(gw_vbat) or math.isinf(gw_vbat)):
                gw_vbat = None

            if epoch_tracker is not None:
                iso_ts = epoch_tracker.get_sample_iso(device_id, ts_us, arrival_wall_time)
            else:
                iso_ts = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

            return {
                "node_id": device_id,
                "timestamp_us": ts_us,
                "timestamp_iso": iso_ts,
                "x": x,
                "y": y,
                "z": z,
                "status_hex": clean_status,
                "status_int": status_int,
                "temp": temp,
                "vbat": vbat,
                "rssi": rssi,
                "gw_vbat": gw_vbat
            }
        elif len(parts) == 5:
            device_id = "LOCAL_NODE"
            ts_us = float(parts[0])
            x = float(parts[1])
            y = float(parts[2])
            z = float(parts[3])
            if math.isnan(x) or math.isinf(x) or math.isnan(y) or math.isinf(y) or math.isnan(z) or math.isinf(z):
                return None

            clean_status = parts[4].strip().split()[0]
            status_int = int(clean_status, 16)

            if epoch_tracker is not None:
                iso_ts = epoch_tracker.get_sample_iso(device_id, ts_us, arrival_wall_time)
            else:
                iso_ts = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

            return {
                "node_id": device_id,
                "timestamp_us": ts_us,
                "timestamp_iso": iso_ts,
                "x": x,
                "y": y,
                "z": z,
                "status_hex": clean_status,
                "status_int": status_int,
                "temp": None,
                "vbat": None,
                "rssi": None,
                "gw_vbat": None
            }
    except (ValueError, IndexError):
        pass

    return None


def parse_telemetry_batch(
    raw_payload: str,
    arrival_wall_time: Optional[float] = None,
    epoch_tracker: Optional[NodeEpochTracker] = None
) -> list:
    """
    Parses a multi-line CSV payload buffer (e.g. UDP packet or batch queue flush).
    If epoch_tracker is provided and has an established epoch for the node, uses
    locked epoch tracking to reconstruct exact historical UTC timestamps even for backlog.
    Otherwise, applies relative microsecond delta-t back-calculation anchored to arrival_wall_time.
    """
    if arrival_wall_time is None:
        arrival_wall_time = time.time()

    lines = [l.strip() for l in raw_payload.splitlines() if l.strip()]
    parsed_samples = [
        parse_telemetry_line(l, arrival_wall_time=arrival_wall_time, epoch_tracker=epoch_tracker)
        for l in lines
    ]
    parsed_samples = [s for s in parsed_samples if s is not None]

    if not parsed_samples:
        return []

    # If epoch_tracker was supplied and had an established epoch before this batch,
    # the sample ISO strings from parse_telemetry_line are already calibrated to true historical UTC!
    first_node = parsed_samples[0]["node_id"]
    if epoch_tracker is not None and epoch_tracker.has_node(first_node):
        return parsed_samples

    # Fallback for standalone / untracked batch: relative microsecond delta-t back-calculation
    latest_us = parsed_samples[-1]["timestamp_us"]
    for s in parsed_samples:
        delta_sec = (latest_us - s["timestamp_us"]) / 1_000_000.0
        sample_utc = arrival_wall_time - delta_sec
        s["timestamp_iso"] = datetime.fromtimestamp(sample_utc, tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    return parsed_samples
