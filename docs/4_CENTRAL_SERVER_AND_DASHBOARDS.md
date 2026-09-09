# 4. Central Server & Dashboards Guide

This guide covers running the self-contained FastAPI Central Server, managing Docker containers, accessing live dashboards, and using the Desktop PyQt5 application.

---

## 1. Quick Start: Managing the Central Server (`manage.sh`)

The central service is managed using the unified [`central_service/manage.sh`](file:///home/morgan/Gropbox/SMACT2026/remote_3_axis_magnetometer/central_service/manage.sh) script:

```bash
cd central_service

# Check service and database status
./manage.sh status

# Start Central Server in background
./manage.sh start

# Follow live server logs
./manage.sh logs -f

# Stop server
./manage.sh stop

# Create SQLite database backup
./manage.sh backup

# Vacuum and optimize SQLite database indices
./manage.sh vacuum
```

---

## 2. Docker Deployment

To launch the server via Docker Compose:

```bash
cd central_service
docker compose up -d

# Check health
docker compose ps
```

The container automatically exposes:
* **Port 8000:** FastAPI REST API, WebSockets, and Web UI.
* **Port 9876 (UDP):** Direct background packet listener for Wi-Fi sensor nodes.

---

## 3. REST API & WebSocket Endpoints

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/` | `GET` | Interactive HTML/JS Web Dashboard with real-time vector graphs |
| `/api/v1/telemetry` | `POST` | Ingest single telemetry point |
| `/api/v1/telemetry/batch` | `POST` | Ingest 10-sample batch payload |
| `/api/v1/nodes` | `GET` | List active sensor nodes and their last-seen status |
| `/api/v1/query` | `GET` | Query time-series telemetry with time/node filters |
| `/api/v1/export/csv` | `GET` | Stream CSV data export |
| `/api/v1/export/hdf5` | `GET` | Stream HDF5 scientific data container |
| `/ws/live` | `WebSocket` | Real-time WebSocket telemetry broadcast |
| `/health` | `GET` | System health check and database statistics |

---

## 4. Desktop PyQt5 Application (`desktop_app/`)

For lab testing and direct USB/UDP recording to HDF5:

```bash
cd desktop_app
pip install -r requirements.txt
python3 main.py
```

Features:
* Live high-speed 3-axis vector plotting ($B_x, B_y, B_z, |B|$).
* Direct serial capture from `/dev/ttyACM*`.
* Instant recording to standard `.h5` / `.npy` format.
