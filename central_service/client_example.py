"""
Future-Proof Client Data Access Example (`client_example.py`)

Demonstrates how researchers can easily query, filter, and load recorded magnetometer data
into Pandas DataFrames, NumPy arrays, or Parquet files for analysis.
"""

import io
import requests
import pandas as pd
import numpy as np

SERVER_URL = "http://localhost:8000" # Change to http://<raspberrypi-ip>:8000

print("=== Magnetometer Data Access Examples ===")

# 1. Fetch data directly into a Pandas DataFrame via CSV API (/api/v1/data)
print("\n1. Fetching data subset into Pandas DataFrame (CSV)...")
csv_url = f"{SERVER_URL}/api/v1/data?node_id=NODE_01&format=csv"
df = pd.read_csv(csv_url)
print(df.head())
print(f"Loaded {len(df)} rows.")

# 2. Fetch downsampled trend data (1-minute averages over a date range)
print("\n2. Fetching 1-minute averaged trend data...")
trend_url = f"{SERVER_URL}/api/v1/data?node_id=NODE_01&downsample_sec=60&format=csv"
df_trend = pd.read_csv(trend_url)
print(df_trend.head())

# 3. Fetch data directly into NumPy arrays via compressed .npz API
print("\n3. Fetching data subset into NumPy arrays (.npz)...")
npz_url = f"{SERVER_URL}/api/v1/data?node_id=NODE_01&format=npz"
res_npz = requests.get(npz_url)

if res_npz.status_code == 200:
    npz_data = np.load(io.BytesIO(res_npz.content))
    x_nT = npz_data["x_nT"]
    y_nT = npz_data["y_nT"]
    z_nT = npz_data["z_nT"]
    mag_nT = npz_data["magnitude_nT"]
    timestamps = npz_data["timestamp"]
    
    print(f"NumPy arrays loaded successfully: {len(x_nT)} samples.")
    print(f"Mean magnetic field magnitude: {np.mean(mag_nT):.2f} nT")

# 4. Fetch Apache Parquet file (.parquet) for ultra-fast columnar loading
try:
    print("\n4. Fetching Apache Parquet dataset (.parquet)...")
    parquet_url = f"{SERVER_URL}/api/v1/data?node_id=NODE_01&format=parquet"
    res_parquet = requests.get(parquet_url)
    if res_parquet.status_code == 200:
        df_parquet = pd.read_parquet(io.BytesIO(res_parquet.content))
        print(df_parquet.head())
        print(f"Parquet loaded successfully: {len(df_parquet)} rows.")
except Exception as e:
    print(f"Parquet load skipped: {e} (install pyarrow/fastparquet if needed)")

# 5. Direct SQLite Connection (Local or via SSH/SCP)
# import sqlite3
# conn = sqlite3.connect("magnetometer.db")
# df = pd.read_sql("SELECT * FROM telemetry WHERE node_id = 'NODE_01'", conn)
