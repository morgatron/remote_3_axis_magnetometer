import time
import math
import random
import requests
from datetime import datetime, timezone

SERVER_URL = "http://localhost:8000"

NODES = [
    {"node_id": "NODE_01", "name": "Station North", "lat": -33.8688, "lon": 151.2093, "bx": 23400.0, "by": -4100.0, "bz": 48900.0},
    {"node_id": "NODE_02", "name": "Station South", "lat": -33.7961, "lon": 151.1780, "bx": 23410.0, "by": -4090.0, "bz": 48915.0},
    {"node_id": "NODE_03", "name": "Station East",  "lat": -33.8150, "lon": 151.0011, "bx": 23390.0, "by": -4115.0, "bz": 48880.0},
    {"node_id": "NODE_04", "name": "Station West",  "lat": -33.8915, "lon": 151.2767, "bx": 23425.0, "by": -4080.0, "bz": 48930.0},
]

def run_simulation(interval_sec=1.0, duration_sec=30):
    print(f"Sending simulated telemetry from {len(NODES)} nodes to {SERVER_URL} for {duration_sec}s...")
    start_t = time.time()
    step = 0

    while time.time() - start_t < duration_sec:
        step += 1
        now_utc = datetime.now(timezone.utc).isoformat()
        
        # Simulate a regional geomagnetic wave anomaly starting at step 10
        anomaly = 0.0
        if 10 <= step <= 25:
            anomaly = 50.0 * math.sin((step - 10) * math.pi / 15.0)

        for n in NODES:
            bx = n["bx"] + anomaly + random.gauss(0, 0.5)
            by = n["by"] + (anomaly * 0.2) + random.gauss(0, 0.5)
            bz = n["bz"] - (anomaly * 0.4) + random.gauss(0, 0.8)

            payload = {
                "node_id": n["node_id"],
                "timestamp": now_utc,
                "x": round(bx, 2),
                "y": round(by, 2),
                "z": round(bz, 2),
                "temp": 24.0,
                "vbat": 3950,
                "lat": n["lat"],
                "lon": n["lon"]
            }

            try:
                requests.post(f"{SERVER_URL}/api/telemetry", json=payload, timeout=2.0)
            except Exception as e:
                print(f"Error posting sample for {n['node_id']}: {e}")

        print(f"[{now_utc}] Step {step}: Sent samples for {len(NODES)} nodes (Anomaly: {anomaly:.1f} nT)")
        time.sleep(interval_sec)

if __name__ == "__main__":
    run_simulation(interval_sec=1.0, duration_sec=20)
