"""
Example script showing how users can easily download and load magnetometer data subsets
directly into Pandas DataFrames or NumPy arrays for custom analysis.
"""

import pandas as pd
import numpy as np
import io
import requests

SERVER_URL = "http://localhost:8000" # Change to http://<raspberrypi-ip>:8000

# 1. Fetch data directly into a Pandas DataFrame via CSV API
print("1. Fetching subset into Pandas DataFrame...")
url = f"{SERVER_URL}/api/data?node_id=NODE_01&format=csv"
df = pd.read_csv(url)
print(df.head())
print(f"Loaded {len(df)} rows.")

# 2. Fetch data directly into NumPy arrays via .npz API
print("\n2. Fetching subset into NumPy arrays...")
url_npz = f"{SERVER_URL}/api/data?node_id=NODE_01&format=npz"
response = requests.get(url_npz)

if response.status_code == 200:
    data = np.load(io.BytesIO(response.content))
    x = data["x"]
    y = data["y"]
    z = data["z"]
    timestamps = data["timestamp"]
    
    mag = np.sqrt(x**2 + y**2 + z**2)
    print(f"NumPy arrays loaded successfully: {len(x)} samples.")
    print(f"Mean magnetic field magnitude: {np.mean(mag):.2f} nT")

# 3. Direct SQLite database access (if running locally or over SSH)
# import sqlite3
# conn = sqlite3.connect("magnetometer.db")
# df = pd.read_sql("SELECT * FROM telemetry WHERE node_id = 'NODE_01'", conn)
