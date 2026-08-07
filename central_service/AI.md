# Magnetometer Central Data Server (`central_service`)

## Overview

The `central_service` is a lightweight, Raspberry Pi-friendly central data ingestion and monitoring service designed for distributed 3-axis magnetometer sensor networks (10 to 50 nodes operating at ~1 Hz down to 1 sample/minute).

Built with **FastAPI** and **SQLite** (WAL mode), it requires zero heavy database or broker setup and operates with a minimal memory footprint (< 30 MB RAM).

---

## Core Features

1. **HTTP Ingestion Endpoints**:
   - Single sample HTTP POST (`/api/telemetry`)
   - Batch sample HTTP POST (`/api/telemetry/batch`) for store-and-forward recovery after network disconnections
2. **Real-Time Web Monitoring Dashboard (`/`)**:
   - Built-in single-page dashboard displaying active node status, latest readings, and real-time interactive time-series plots ($X, Y, Z$, Magnitude) via WebSockets (`/ws/live`).
3. **Flexible Data Subset Exporter**:
   - Direct download of data subsets filtered by `node_id`, `start`, and `end` timestamps in **CSV**, **JSON**, or compressed **NumPy array (`.npz`)** formats.
4. **Researcher / Python Integration**:
   - Load subsets into Pandas in one line: `pd.read_csv("http://<server-ip>:8000/api/data?node_id=NODE_01&format=csv")`
   - Or connect directly to `magnetometer.db` via SQLite.

---

## File Structure

```
central_service/
├── server.py             # Single-file FastAPI + SQLite backend server & WebSocket hub
├── static/
│   └── index.html        # Real-time web GUI monitoring dashboard
├── requirements.txt      # Python dependencies (fastapi, uvicorn, pydantic, numpy, pandas)
├── client_example.py     # Example Python script loading data into Pandas/NumPy
├── test_simulator.py     # Multi-node simulator for testing 1 Hz telemetry & magnetic transients
├── docker-compose.yml    # Optional single-container Docker deployment configuration
└── Dockerfile            # Lightweight Python container setup
```

---

## Quick Start

### 1. Run Server Natively (Raspberry Pi / Linux / macOS / Windows)
```bash
cd central_service
pip install -r requirements.txt
python server.py
```

### 2. Access Web GUI
Open `http://localhost:8000` (or `http://<pi-ip>:8000`) in your web browser.

### 3. Test with Simulated Telemetry
In a separate terminal:
```bash
python test_simulator.py
```
