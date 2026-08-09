# Magnetometer Central Data Server & Gateway Services (`central_service`)

## Overview

The `central_service` is a future-proof, lightweight, Raspberry Pi-friendly central data ingestion, edge gateway, and monitoring platform designed for long-term time-series recording of distributed 3-axis magnetometer networks (10 to 50 nodes operating at ~1 Hz down to 1 sample/minute).

Built with **FastAPI** and **SQLite** (WAL mode), it operates with zero heavy database setup and minimal memory footprint (< 30 MB RAM).

---

## API v1 & Data Schema Future-Proofing

1. **Database Schema Stability (`magnetometer.db`)**:
   - `telemetry`: `timestamp`, `node_id`, `x`, `y`, `z`, `units` ('nT'), `temp`, `vbat`, `rssi`, `status_flags`, `extra_json`.
   - `nodes`: `node_id`, `name`, `lat`, `lon`, `elevation_m`, `last_seen`, `sensor_model`, `cycle_count`, `baseline_x`, `baseline_y`, `baseline_z`, `notes`.
   - **Automatic Safe Migrations**: `init_db()` automatically runs non-destructive `ALTER TABLE ADD COLUMN` checks on startup, ensuring past SQLite database files remain 100% compatible.

2. **API Versioning & Forward Compatibility**:
   - Primary REST endpoints are versioned under `/api/v1/...` (`/api/v1/telemetry`, `/api/v1/telemetry/batch`, `/api/v1/data`, `/api/v1/nodes`).
   - Legacy routes (`/api/telemetry`, `/api/data`, `/api/nodes`) are maintained as permanent alias routes for 100% backward compatibility.

3. **Data Export Formats**:
   - **CSV (`.csv`)**: Standardized headers (`timestamp_utc`, `node_id`, `x_nT`, `y_nT`, `z_nT`, `magnitude_nT`, `temp_c`, `vbat_mv`).
   - **Apache Parquet (`.parquet`)**: Ultra-fast, compressed columnar binary format for scientific data.
   - **NumPy Compressed (`.npz`)**: Native array export for Python (`x_nT`, `y_nT`, `z_nT`, `magnitude_nT`).
   - **JSON**: Structured REST responses with `schema_version: "1.0"`.

4. **Server-Side Downsampling**:
   - Use `downsample_sec=60` (1-min) or `3600` (1-hour) on `/api/v1/data` to query multi-month datasets at high speeds.

5. **Edge Arrival Timestamping & $\Delta t$ Relative Reconstruction**:
   - Microcontrollers stream raw microsecond uptimes (`timestamp_us`) without requiring battery-backed RTCs or NTP client code.
   - Upon arrival at `gateway.py`, telemetry packets are stamped with the gateway's system UTC wall clock.
   - Multi-sample batches (e.g. after network drops or Store-and-Forward queue flushes) are processed via `parse_telemetry_batch()`, which anchors to the latest arrival time and uses relative microsecond $\Delta t$ back-calculation to reconstruct exact historical sample spacing without clock drift.

---

## File Structure

```
central_service/
├── server.py                 # Single-file FastAPI + SQLite backend server & WebSocket hub
├── gateway.py            # Unified UDP, BLE, and Serial store-and-forward edge gateway
├── static/
│   └── index.html            # Real-time web GUI monitoring dashboard
├── client_example.py         # Future-proof Python script loading data into Pandas/NumPy/Parquet
├── test_simulator.py         # Multi-node simulator for testing 1 Hz telemetry & magnetic transients
├── requirements.txt          # Python dependencies
├── docker-compose.yml        # Optional single-container Docker deployment configuration
└── Dockerfile                # Lightweight Python container setup
```

---

## Quick Start

### 1. Run Central Server (Raspberry Pi / Linux / PC)
```bash
cd central_service
pip install -r requirements.txt
python server.py
```
Open `http://localhost:8000` (or `http://<pi-ip>:8000`) in your web browser.

### 2. Run Edge Gateway (on Edge Pi / Field Gateway)
```bash
export CENTRAL_SERVER_URL="http://192.168.1.100:8000"
python gateway.py
```
