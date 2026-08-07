# Magnetometer Central Data Server & Gateway Services (`central_service`)

## Overview

The `central_service` is a lightweight, Raspberry Pi-friendly central data ingestion, gateway service, and monitoring platform designed for distributed 3-axis magnetometer sensor networks (10 to 50 nodes operating at ~1 Hz down to 1 sample/minute).

Built with **FastAPI** and **SQLite** (WAL mode), it operates with zero heavy database setup and minimal memory (< 30 MB RAM).

---

## Unified Edge Gateway Architecture (`gateway.py`)

In field deployments where sensor nodes operate on isolated subnets (WiFi UDP), Bluetooth LE (BLE), or USB/Serial interfaces without direct internet connectivity, a single **Unified Gateway (`gateway.py`)** manages all interfaces concurrently:

```
[Field Sensor Layer]                          [Edge Gateway Layer]                        [Central Data Server]

ESP32 Node 01 (WiFi UDP)  ──┐
ESP32 Node 02 (WiFi UDP)  ──┼─► [gateway.py] ───┐
ESP32 Node 03 (USB Serial)─┤    (Unified        ├──(HTTP POST /api/telemetry/batch)──► [server.py (FastAPI)]
ESP32 Node 04 (BLE 5.0)   ──┤     Store-and-    │                                           │
ESP32 Node 05 (BLE 5.0)   ──┘     Forward)      │                                      SQLite Database
                                                │                                     (magnetometer.db)
```

### Gateway Responsibilities (`gateway.py`):
1. **Multi-Interface Ingestion**: Runs UDP (Port 9876), BLE Central Scanner (Nordic UART Service), and Serial/USB listeners in parallel threads.
2. **Unified Store-and-Forward Queue**: Buffers incoming samples from all interfaces. If the connection to the central server drops, queued samples are preserved and flushed in batch upon reconnection.
3. **Graceful Fallbacks**: Automatically degrades gracefully (e.g., if `bleak` is not installed, BLE is skipped while UDP/Serial continue running).

---

## File Structure

```
central_service/
├── server.py             # Single-file FastAPI + SQLite backend server & WebSocket hub
├── gateway.py            # Unified UDP, BLE, and Serial store-and-forward edge gateway
├── static/
│   └── index.html        # Real-time web GUI monitoring dashboard
├── client_example.py     # Example Python script loading data into Pandas/NumPy
├── test_simulator.py     # Multi-node simulator for testing 1 Hz telemetry & magnetic transients
├── requirements.txt      # Python dependencies
├── docker-compose.yml    # Optional single-container Docker deployment configuration
└── Dockerfile            # Lightweight Python container setup
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

### 2. Run Unified Gateway (on Edge Pi / Field Gateway)
```bash
export CENTRAL_SERVER_URL="http://192.168.1.100:8000"
python gateway.py
```

### 3. Optional Environment Variables for `gateway.py`
- `CENTRAL_SERVER_URL`: Target server URL (default: `http://localhost:8000`).
- `ENABLE_UDP`: Enable/disable WiFi UDP listener (default: `true`, port `9876`).
- `ENABLE_BLE`: Enable/disable Bluetooth LE listener (default: `true`).
- `ENABLE_SERIAL`: Enable/disable USB Serial listener (default: `false`).
- `SERIAL_PORT`: e.g. `/dev/ttyUSB0`.
