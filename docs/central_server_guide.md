# Central Server & Web GUI Guide (`docs/central_server_guide.md`)

This guide covers operating the **Central Data Server** (`central_service/server.py`), the **Edge Gateway Relay** (`central_service/gateway.py`), accessing the Web GUI, and exporting telemetry data.

---

## 1. Running the Services

### Central Server (`server.py`)
Provides SQLite persistence, WebSockets live streaming, REST API endpoints, and the Web GUI interface.

```bash
cd central_service
python server.py
```
- Server URL: `http://localhost:8000`
- Interactive OpenAPI Docs: `http://localhost:8000/docs`

### Edge Gateway Relay (`gateway.py`)
Reads incoming USB Serial lines from receiver MCUs or UDP port 9876 broadcasts, and forwards batched payloads to the server API.

```bash
cd central_service
python gateway.py
```

---

## 2. Web GUI Features

Access `http://localhost:8000` in any modern web browser:

1. **Active Node Cards**: Real-time signal strength (RSSI), battery voltage ($V_{bat}$), sample rate (Hz), and total packet count per node.
2. **Time-Series Plots**: Live 3-axis magnetic field graph ($B_x, B_y, B_z$ in nT) powered by WebSockets.
3. **Power Spectral Density (PSD)**: Real-time FFT spectrum display with noise floor analysis ($\text{pT}/\sqrt{\text{Hz}}$).
4. **Data Exporter**: Export raw or downsampled telemetry directly from the web browser.

---

## 3. REST API v1 Reference

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `GET` | `/health` | Server health check & active database status |
| `POST` | `/api/v1/telemetry/batch` | Ingest batched array of sensor telemetry items |
| `GET` | `/api/v1/nodes` | List active remote sensor nodes with RSSI & battery metrics |
| `GET` | `/api/v1/telemetry/recent` | Query recent telemetry points with limit and downsampling |
| `GET` | `/api/v1/data/export` | Download exported data in CSV, Parquet, NumPy, or JSON format |
| `WS` | `/ws/telemetry` | WebSocket stream for live time-series plotting |

---

## 4. Supported Export Formats

Telemetry can be exported via the Web GUI or API using the `/api/v1/data/export` endpoint:

1. **CSV (`format=csv`)**: Human-readable 6-column tabular data stream.
2. **Apache Parquet (`format=parquet`)**: High-performance, columnar compressed binary format (Snappy/ZSTD compression). Ideal for Pandas/Polars data science pipelines.
3. **NumPy Array (`format=npz`)**: Compressed `.npz` archive containing `timestamps`, `x_nT`, `y_nT`, and `z_nT` arrays.
4. **JSON (`format=json`)**: Structured JSON list of telemetry objects.

### Example Export Python Script
```python
import requests

url = "http://localhost:8000/api/v1/data/export?format=parquet&limit=10000"
response = requests.get(url)

with open("telemetry_export.parquet", "wb") as f:
    f.write(response.content)

print("Saved telemetry_export.parquet successfully.")
```
