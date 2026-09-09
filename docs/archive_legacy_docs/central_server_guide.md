# Central Server & Web GUI Guide (`docs/central_server_guide.md`)

This guide covers operating the **Central Data Server** (`central_service/server.py`), the **Edge Gateway Relay** (`central_service/gateway.py`), accessing the Web GUI, and exporting telemetry data.

---

## 1. Quick Setup & Running the Services

### Automated Installation (Raspberry Pi 4 / Laptop)
Run the automated installer script to set up Python virtual environments, configure `.env`, install auto-starting systemd services, and apply hardware optimizations:

```bash
cd central_service
sudo ./install.sh
```

### Managing the Server (`./manage.sh`)
```bash
./manage.sh status                  # Display status, database size, memory, and telemetry counts
./manage.sh logs                    # Live stream logs
./manage.sh restart                 # Restart services
./manage.sh backup                  # Safe online SQLite snapshot into backups/
./manage.sh nodes                   # List all registered sensor nodes and GPS coordinates
./manage.sh node delete TEST        # Delete unwanted/test node (add --purge to delete telemetry)
./manage.sh node prune --days 30    # Prune inactive nodes not seen in >30 days
```

### Manual Execution
```bash
cd central_service
python server.py
# In another terminal / background:
python gateway.py
```
- Server URL: `http://localhost:8000`
- Interactive OpenAPI Docs: `http://localhost:8000/docs`

### Residential NAT & Remote Access (`./remote_access.sh`)
If deploying behind a residential home router with dynamic IP or Carrier-Grade NAT (CGNAT):
```bash
./remote_access.sh
```
See the dedicated [**Residential NAT & Remote Access Guide**](NAT_AND_REMOTE_ACCESS.md) and [**Server Hardware Setup Guide**](SERVER_SETUP_GUIDE.md).

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

---

## 5. Station Metadata & GPS Location Management (`scripts/update_node.py`)

To assign or update station names, GPS coordinates, elevation, and site notes for deployed nodes:

```bash
# 1. Update station location and site notes
python3 scripts/update_node.py --node NODE_3A8 --name "North Ridge" --lat -33.8568 --lon 151.2153 --elev 42.5 --notes "Borehole #3 site"

# 2. List all registered nodes and their coordinates
python3 scripts/update_node.py --list

# 3. Interactive prompt wizard (prompts for fields interactively)
python3 scripts/update_node.py -i

# 4. Delete an unwanted test node (add --purge to also delete historical telemetry)
python3 scripts/update_node.py --delete TEST_NODE --purge

# 5. Prune all nodes inactive for more than 30 days
python3 scripts/update_node.py --prune-inactive 30 --purge
```
