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

## Open-Internet Security & API Key Authentication

To secure the server against unauthorized open-internet data injection:

1. **Server Configuration**: Set `export API_KEY="your_secret_key"` before launching `server.py`.
   - All state mutation endpoints (`POST /api/v1/telemetry`, `POST /api/v1/telemetry/batch`, `POST /api/v1/nodes/update`) automatically require `X-API-Key: your_secret_key` in the request header.
   - If `API_KEY` is omitted or empty, authentication is disabled for local offline testing.
   - Read-only endpoints (`GET /`, `GET /health`, `GET /api/v1/nodes`, `GET /api/v1/data`) remain open so dashboards can display telemetry without authentication.

2. **Gateway Configuration**: Set matching `export API_KEY="your_secret_key"` before running `gateway.py`:
   ```bash
   export API_KEY="your_secret_key"
   export CENTRAL_SERVER_URL="http://192.168.1.100:8000"
   python gateway.py
   ```

---

## Web GUI Dashboard Features (`static/index.html`)

- **Signal Strength (RSSI) Plotting**: Real-time signal strength (in $\text{dBm}$) is stored in the database, displayed on each node card, and selectable in the field dropdown.
- **Historical Time-Range Selector**: Select **Live Stream**, **Last 1 Hour** (10s avg), **Last 6 Hours** (1m avg), **Last 24 Hours** (1m avg), or **Last 7 Days** (1h avg) to analyze database trends directly in the browser.
- **Multi-Node Overlay Plotting**: Select **Overlay: All Nodes** ($|B|$, $B_z$, or $\text{RSSI}$) to plot multiple field nodes simultaneously on a single chart with distinct color-coded lines for spatial gradient comparison.

---

## File Structure

```
central_service/
├── install.sh                # Automated one-click installer for Raspberry Pi 4 & Linux Laptops
├── manage.sh                 # Unified operations CLI (status, start, stop, restart, logs, backup, export)
├── remote_access.sh          # Residential NAT assistant (Tailscale, Cloudflare Tunnels, CGNAT check)
├── server.py                 # FastAPI + SQLite backend server & WebSocket hub (.env supported)
├── gateway.py                # Unified UDP, BLE, and Serial store-and-forward edge gateway
├── stream_parser.py          # Shared parsing logic for MCU telemetry lines & microsecond delta-t batches
├── static/
│   └── index.html            # Real-time web GUI monitoring dashboard
├── systemd/                  # Systemd service templates for 24/7 background operation
│   ├── magnetometer-server.service
│   └── magnetometer-gateway.service
├── .env.example              # Environment variables template (ports, paths, API keys)
├── client_example.py         # Future-proof Python script loading data into Pandas/NumPy/Parquet
├── test_server.py            # Automated end-to-end HTTP test suite
├── test_simulator.py         # Multi-node simulator for testing 1 Hz telemetry & magnetic transients
├── requirements.txt          # Python dependencies
├── docker-compose.yml        # Docker Compose configuration (server + gateway profiles)
└── Dockerfile                # Lightweight Python container setup with healthcheck
```

---

## Quick Start & Deployment

### Method 1: Automated Installer (Raspberry Pi 4 / Laptop) - Recommended
```bash
cd central_service
sudo ./install.sh
```
This automatically sets up Python virtual environments, systemd auto-restart services, laptop lid-close behavior, and SD card safety.

### Method 2: Management Script (`manage.sh`)
```bash
./manage.sh status     # Check health, database size, memory, and telemetry count
./manage.sh logs       # Live stream logs
./manage.sh backup     # Safe online SQLite backup
./manage.sh export csv # Quick CLI export
```

### Method 3: Running Behind Residential NAT / CGNAT
```bash
./remote_access.sh     # Interactive assistant for Cloudflare Tunnels (Recommended) & NAT diagnostics
```

### Method 4: Docker Compose
```bash
docker compose up -d                  # Server only
docker compose --profile all up -d    # Server + UDP Gateway
```

