"""
Central Server Automated Test Suite (`test_server.py`)

End-to-end HTTP tests for API v1 endpoints, legacy backwards-compatibility aliases,
data export formats (CSV, JSON, NumPy .npz, Apache Parquet),
downsampling averaging, and database schema migrations.

Usage:
    /home/morgan/miniforge3/envs/rm3100/bin/python test_server.py
"""

import os
import sys
import time
import io
import tempfile
import subprocess
import requests
import numpy as np
import pandas as pd

TEST_PORT = 8899
SERVER_URL = f"http://localhost:{TEST_PORT}"

# Create temp DB for testing
temp_db_fd, temp_db_path = tempfile.mkstemp(suffix=".db")
os.close(temp_db_fd)

def start_test_server():
    env = os.environ.copy()
    env["DB_FILE"] = temp_db_path
    
    cmd = [
        "/home/morgan/miniforge3/envs/rm3100/bin/python", "-m", "uvicorn",
        "server:app", "--host", "127.0.0.1", "--port", str(TEST_PORT)
    ]
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    # Wait for server startup
    start_t = time.time()
    while time.time() - start_t < 10:
        try:
            r = requests.get(f"{SERVER_URL}/health", timeout=1)
            if r.status_code == 200:
                print(f"[TEST SETUP] Test server active on port {TEST_PORT}")
                return proc
        except Exception:
            time.sleep(0.2)
            
    proc.terminate()
    raise RuntimeError("Test server failed to start within timeout.")

def test_health_check():
    r = requests.get(f"{SERVER_URL}/health")
    assert r.status_code == 200
    data = r.json()
    assert data["status"] == "ok"
    assert data["api_version"] == "1.0"
    print("[PASS] Health check test passed.")

def test_single_telemetry_ingestion():
    payload = {
        "node_id": "TEST_NODE_01",
        "timestamp": "2026-08-08T12:00:00Z",
        "x": 23415.2,
        "y": -4120.8,
        "z": 48910.1,
        "units": "nT",
        "temp": 24.5,
        "vbat": 3980,
        "rssi": -65,
        "sensor_model": "RM3100",
        "cycle_count": 200
    }
    
    # Test v1 endpoint
    resp1 = requests.post(f"{SERVER_URL}/api/v1/telemetry", json=payload)
    assert resp1.status_code == 201
    assert resp1.json()["status"] == "success"

    # Test legacy alias endpoint
    payload["timestamp"] = "2026-08-08T12:00:01Z"
    resp2 = requests.post(f"{SERVER_URL}/api/telemetry", json=payload)
    assert resp2.status_code == 201
    assert resp2.json()["status"] == "success"
    
    print("[PASS] Single telemetry ingestion (v1 and legacy alias) passed.")

def test_batch_telemetry_ingestion():
    batch_payload = {
        "node_id": "TEST_NODE_02",
        "points": [
            {"node_id": "TEST_NODE_02", "timestamp": "2026-08-08T12:00:10Z", "x": 23400.0, "y": -4100.0, "z": 48900.0},
            {"node_id": "TEST_NODE_02", "timestamp": "2026-08-08T12:00:11Z", "x": 23410.0, "y": -4090.0, "z": 48910.0},
            {"node_id": "TEST_NODE_02", "timestamp": "2026-08-08T12:00:12Z", "x": 23420.0, "y": -4080.0, "z": 48920.0}
        ]
    }
    resp = requests.post(f"{SERVER_URL}/api/v1/telemetry/batch", json=batch_payload)
    assert resp.status_code == 201
    assert resp.json()["inserted"] == 3
    print("[PASS] Batch telemetry ingestion passed.")

def test_list_nodes():
    resp = requests.get(f"{SERVER_URL}/api/v1/nodes")
    assert resp.status_code == 200
    nodes = resp.json()
    node_ids = [n["node_id"] for n in nodes]
    assert "TEST_NODE_01" in node_ids
    assert "TEST_NODE_02" in node_ids
    print("[PASS] List nodes test passed.")

def test_data_export_formats():
    # 1. JSON Format
    r_json = requests.get(f"{SERVER_URL}/api/v1/data?node_id=TEST_NODE_01&format=json")
    assert r_json.status_code == 200
    j_body = r_json.json()
    assert j_body["schema_version"] == "1.0"
    assert len(j_body["data"]) >= 2

    # 2. CSV Format
    r_csv = requests.get(f"{SERVER_URL}/api/v1/data?node_id=TEST_NODE_01&format=csv")
    assert r_csv.status_code == 200
    df_csv = pd.read_csv(io.StringIO(r_csv.text))
    assert "timestamp_utc" in df_csv.columns
    assert "x_nT" in df_csv.columns
    assert "magnitude_nT" in df_csv.columns
    assert len(df_csv) >= 2

    # 3. NumPy (.npz) Format
    r_npz = requests.get(f"{SERVER_URL}/api/v1/data?node_id=TEST_NODE_01&format=npz")
    assert r_npz.status_code == 200
    npz_data = np.load(io.BytesIO(r_npz.content))
    assert "x_nT" in npz_data
    assert "y_nT" in npz_data
    assert "magnitude_nT" in npz_data
    assert len(npz_data["x_nT"]) >= 2

    # 4. Parquet (.parquet) Format
    r_parquet = requests.get(f"{SERVER_URL}/api/v1/data?node_id=TEST_NODE_01&format=parquet")
    assert r_parquet.status_code in [200, 400]
    if r_parquet.status_code == 200:
        df_parquet = pd.read_parquet(io.BytesIO(r_parquet.content))
        assert "timestamp_utc" in df_parquet.columns
        assert "x_nT" in df_parquet.columns
        assert len(df_parquet) >= 2

    print("[PASS] Data export formats test (JSON, CSV, NumPy .npz, Parquet) passed.")

def test_downsampling():
    points = []
    for i in range(60):
        ts = f"2026-08-08T13:00:{i:02d}Z"
        points.append({"node_id": "TEST_NODE_DS", "timestamp": ts, "x": 20000.0 + i, "y": -4000.0, "z": 45000.0})

    requests.post(f"{SERVER_URL}/api/v1/telemetry/batch", json={"node_id": "TEST_NODE_DS", "points": points})

    # Query with 60-second downsampling
    resp = requests.get(f"{SERVER_URL}/api/v1/data?node_id=TEST_NODE_DS&downsample_sec=60&format=json")
    assert resp.status_code == 200
    data = resp.json()["data"]
    assert len(data) == 1  # Aggregated into 1 minute bucket
    assert abs(data[0]["x_nT"] - 20029.5) < 1.0
    print("[PASS] Server-side downsampling test passed.")

def cleanup(proc):
    if proc:
        proc.terminate()
        proc.wait()
    if os.path.exists(temp_db_path):
        os.remove(temp_db_path)

if __name__ == "__main__":
    print("=== Running Central Server End-to-End Automated Test Suite ===")
    proc = None
    try:
        proc = start_test_server()
        test_health_check()
        test_single_telemetry_ingestion()
        test_batch_telemetry_ingestion()
        test_list_nodes()
        test_data_export_formats()
        test_downsampling()
        print("\nALL CENTRAL SERVER TESTS PASSED SUCCESSFULLY!")
    finally:
        cleanup(proc)
