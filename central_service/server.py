import sqlite3
import os
import io
import json
import math
import logging
from datetime import datetime, timezone
from typing import Optional, List, Union

from fastapi import FastAPI, HTTPException, Query, Response, WebSocket, WebSocketDisconnect, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse, StreamingResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field
import numpy as np
import pandas as pd

# Try loading local .env file if present
env_path = os.path.join(os.path.dirname(__file__), ".env")
if os.path.exists(env_path):
    try:
        with open(env_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    k = k.strip()
                    v = v.strip().strip("'\"")
                    if k and k not in os.environ:
                        os.environ[k] = v
    except Exception:
        pass

# Configure Logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s"
)
logger = logging.getLogger("central_server")

DB_FILE = os.getenv("DB_FILE", "magnetometer.db")
API_KEY = os.getenv("API_KEY", None)
HOST = os.getenv("HOST", "0.0.0.0")
PORT = int(os.getenv("PORT", "8000"))

def get_db():
    db_dir = os.path.dirname(DB_FILE)
    if db_dir and not os.path.exists(db_dir):
        os.makedirs(db_dir, exist_ok=True)
    conn = sqlite3.connect(DB_FILE)
    conn.row_factory = sqlite3.Row
    return conn

def init_db():
    """Initializes database tables and safely applies migrations for missing columns."""
    with get_db() as conn:
        conn.execute("PRAGMA journal_mode=WAL;")
        
        # 1. Telemetry Table
        conn.execute("""
        CREATE TABLE IF NOT EXISTS telemetry (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            node_id TEXT NOT NULL,
            x REAL NOT NULL,
            y REAL NOT NULL,
            z REAL NOT NULL,
            units TEXT DEFAULT 'nT',
            temp REAL,
            vbat INTEGER,
            rssi INTEGER,
            status_flags TEXT DEFAULT '0xC00000',
            extra_json TEXT
        );
        """)
        
        # Indexes for high-speed time-range and spatial slicing
        conn.execute("CREATE INDEX IF NOT EXISTS idx_telemetry_node_time ON telemetry(node_id, timestamp);")
        conn.execute("CREATE INDEX IF NOT EXISTS idx_telemetry_time ON telemetry(timestamp);")

        # 2. Nodes Registry Table
        conn.execute("""
        CREATE TABLE IF NOT EXISTS nodes (
            node_id TEXT PRIMARY KEY,
            name TEXT,
            lat REAL,
            lon REAL,
            elevation_m REAL DEFAULT 0.0,
            last_seen TEXT,
            sensor_model TEXT DEFAULT 'RM3100',
            cycle_count INTEGER DEFAULT 200,
            baseline_x REAL DEFAULT 0.0,
            baseline_y REAL DEFAULT 0.0,
            baseline_z REAL DEFAULT 0.0,
            notes TEXT
        );
        """)

        # Safe schema migrations for legacy database files
        existing_telemetry_cols = [row[1] for row in conn.execute("PRAGMA table_info(telemetry)").fetchall()]
        for col_name, col_type in [("units", "TEXT DEFAULT 'nT'"), ("temp", "REAL"), ("vbat", "INTEGER"), ("rssi", "INTEGER"), ("status_flags", "TEXT DEFAULT '0xC00000'"), ("extra_json", "TEXT")]:
            if col_name not in existing_telemetry_cols:
                logger.info(f"Applying migration: Adding column '{col_name}' to telemetry table")
                conn.execute(f"ALTER TABLE telemetry ADD COLUMN {col_name} {col_type};")

        existing_node_cols = [row[1] for row in conn.execute("PRAGMA table_info(nodes)").fetchall()]
        for col_name, col_type in [("elevation_m", "REAL DEFAULT 0.0"), ("sensor_model", "TEXT DEFAULT 'RM3100'"), ("cycle_count", "INTEGER DEFAULT 200"), ("baseline_x", "REAL DEFAULT 0.0"), ("baseline_y", "REAL DEFAULT 0.0"), ("baseline_z", "REAL DEFAULT 0.0"), ("notes", "TEXT")]:
            if col_name not in existing_node_cols:
                logger.info(f"Applying migration: Adding column '{col_name}' to nodes table")
                conn.execute(f"ALTER TABLE nodes ADD COLUMN {col_name} {col_type};")

        conn.commit()

init_db()

# WebSocket Manager for Live Broadcasting
class ConnectionManager:
    def __init__(self):
        self.active_connections: List[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)

    async def broadcast(self, message: dict):
        for connection in list(self.active_connections):
            try:
                await connection.send_json(message)
            except Exception:
                self.disconnect(connection)

ws_manager = ConnectionManager()

app = FastAPI(
    title="Magnetometer Central Data Server",
    description="Future-proof, lightweight time-series server for distributed 3-axis magnetometers.",
    version="1.0.0"
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.middleware("http")
async def api_key_middleware(request: Request, call_next):
    if API_KEY and API_KEY.strip():
        # Validate X-API-Key header on all state mutation methods (POST, PUT, DELETE, PATCH)
        if request.method in ["POST", "PUT", "DELETE", "PATCH"]:
            key = request.headers.get("X-API-Key")
            if not key or key.strip() != API_KEY.strip():
                return JSONResponse(
                    status_code=401,
                    content={"detail": "Unauthorized: Invalid or missing X-API-Key header"}
                )
    return await call_next(request)

STATIC_DIR = os.path.join(os.path.dirname(__file__), "static")
if os.path.exists(STATIC_DIR):
    app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")

@app.get("/", response_class=HTMLResponse)
def index_page():
    index_path = os.path.join(STATIC_DIR, "index.html")
    if os.path.exists(index_path):
        with open(index_path, "r", encoding="utf-8") as f:
            return f.read()
    return "<h3>Magnetometer Central Data Server Running</h3>"

# --- Pydantic Data Models (Version 1.0) ---

class TelemetryPoint(BaseModel):
    node_id: str = Field(..., description="Unique hardware identifier string")
    timestamp: Optional[str] = Field(None, description="ISO8601 UTC timestamp string")
    x: float = Field(..., description="X-axis magnetic field in nT")
    y: float = Field(..., description="Y-axis magnetic field in nT")
    z: float = Field(..., description="Z-axis magnetic field in nT")
    units: Optional[str] = Field("nT", description="Physical unit string")
    temp: Optional[float] = Field(None, description="Temperature in Celsius")
    vbat: Optional[int] = Field(None, description="Battery voltage in mV")
    rssi: Optional[int] = Field(None, description="Signal strength in dBm")
    status_flags: Optional[str] = Field("0xC00000", description="Hex status flags")
    lat: Optional[float] = Field(None, description="Latitude in decimal degrees")
    lon: Optional[float] = Field(None, description="Longitude in decimal degrees")
    elevation_m: Optional[float] = Field(None, description="Elevation in meters")
    sensor_model: Optional[str] = Field("RM3100", description="Sensor hardware model")
    cycle_count: Optional[int] = Field(200, description="RM3100 cycle count")
    extra_json: Optional[str] = Field(None, description="Extensible JSON string for custom diagnostics")

class BatchTelemetry(BaseModel):
    node_id: str
    points: List[TelemetryPoint]

class NodeUpdate(BaseModel):
    node_id: str
    name: Optional[str] = None
    lat: Optional[float] = None
    lon: Optional[float] = None
    elevation_m: Optional[float] = None
    baseline_x: Optional[float] = 0.0
    baseline_y: Optional[float] = 0.0
    baseline_z: Optional[float] = 0.0
    notes: Optional[str] = None

# --- Ingestion Helper ---

async def store_telemetry_point(conn, point: TelemetryPoint):
    ts = point.timestamp or datetime.now(timezone.utc).isoformat()
    units = point.units or "nT"
    status_flags = point.status_flags or "0xC00000"
    cycle = point.cycle_count or 200
    model = point.sensor_model or "RM3100"

    conn.execute(
        """
        INSERT INTO telemetry (timestamp, node_id, x, y, z, units, temp, vbat, rssi, status_flags, extra_json)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (ts, point.node_id, point.x, point.y, point.z, units, point.temp, point.vbat, point.rssi, status_flags, point.extra_json)
    )
    
    conn.execute(
        """
        INSERT INTO nodes (node_id, name, lat, lon, elevation_m, last_seen, sensor_model, cycle_count)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(node_id) DO UPDATE SET
            last_seen = excluded.last_seen,
            lat = COALESCE(excluded.lat, nodes.lat),
            lon = COALESCE(excluded.lon, nodes.lon),
            elevation_m = COALESCE(excluded.elevation_m, nodes.elevation_m),
            sensor_model = COALESCE(excluded.sensor_model, nodes.sensor_model),
            cycle_count = COALESCE(excluded.cycle_count, nodes.cycle_count)
        """,
        (point.node_id, point.node_id, point.lat, point.lon, point.elevation_m or 0.0, ts, model, cycle)
    )

# --- API v1 Endpoints ---

@app.get("/health")
def health():
    return {"status": "ok", "api_version": "1.0", "db": DB_FILE}

@app.post("/api/v1/telemetry", status_code=201)
@app.post("/api/telemetry", status_code=201)
async def ingest_sample(point: TelemetryPoint):
    """Ingest a single telemetry reading (HTTP POST). Supported on /api/v1/telemetry and /api/telemetry."""
    ts = point.timestamp or datetime.now(timezone.utc).isoformat()
    with get_db() as conn:
        await store_telemetry_point(conn, point)
        conn.commit()

    # Broadcast live reading via WebSockets
    await ws_manager.broadcast({
        "type": "telemetry",
        "node_id": point.node_id,
        "timestamp": ts,
        "x": point.x,
        "y": point.y,
        "z": point.z,
        "units": point.units or "nT",
        "temp": point.temp,
        "vbat": point.vbat,
        "rssi": point.rssi,
        "sensor_model": point.sensor_model or "RM3100",
        "cycle_count": point.cycle_count or 200
    })
    return {"status": "success", "node_id": point.node_id, "timestamp": ts}

@app.post("/api/v1/telemetry/batch", status_code=201)
@app.post("/api/telemetry/batch", status_code=201)
async def ingest_batch(batch: BatchTelemetry):
    """Ingest a batch of telemetry readings from a node (useful after offline periods)."""
    if not batch.points:
        return {"status": "success", "inserted": 0}

    now_str = datetime.now(timezone.utc).isoformat()
    with get_db() as conn:
        for p in batch.points:
            p.node_id = batch.node_id
            await store_telemetry_point(conn, p)
        conn.commit()

    # Broadcast all points in batch via WebSockets
    for p in batch.points:
        await ws_manager.broadcast({
            "type": "telemetry",
            "node_id": batch.node_id,
            "timestamp": p.timestamp or now_str,
            "x": p.x,
            "y": p.y,
            "z": p.z,
            "units": p.units or "nT",
            "temp": p.temp,
            "vbat": p.vbat,
            "rssi": p.rssi,
            "sensor_model": p.sensor_model or "RM3100",
            "cycle_count": p.cycle_count or 200
        })
    return {"status": "success", "node_id": batch.node_id, "inserted": len(batch.points)}

@app.get("/api/v1/nodes")
@app.get("/api/nodes")
def list_nodes():
    """List all registered nodes, geographic coordinates, and latest readings."""
    with get_db() as conn:
        cursor = conn.execute("""
        SELECT n.node_id, n.name, n.lat, n.lon, n.elevation_m, n.last_seen,
               n.sensor_model, COALESCE(n.cycle_count, 200) as cycle_count,
               n.baseline_x, n.baseline_y, n.baseline_z, n.notes,
               t.x, t.y, t.z, t.temp, t.vbat, t.rssi, t.status_flags
        FROM nodes n
        LEFT JOIN telemetry t ON t.id = (
            SELECT id FROM telemetry WHERE node_id = n.node_id ORDER BY timestamp DESC LIMIT 1
        )
        ORDER BY n.node_id;
        """)
        return [dict(row) for row in cursor.fetchall()]

@app.post("/api/v1/nodes/update")
@app.post("/api/nodes/update")
def update_node(node: NodeUpdate):
    """Update metadata for a node (e.g. lat/lon, elevation, baseline offsets, notes)."""
    with get_db() as conn:
        conn.execute("""
        INSERT INTO nodes (node_id, name, lat, lon, elevation_m, baseline_x, baseline_y, baseline_z, notes, last_seen)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))
        ON CONFLICT(node_id) DO UPDATE SET
            name = COALESCE(excluded.name, nodes.name),
            lat = COALESCE(excluded.lat, nodes.lat),
            lon = COALESCE(excluded.lon, nodes.lon),
            elevation_m = COALESCE(excluded.elevation_m, nodes.elevation_m),
            baseline_x = COALESCE(excluded.baseline_x, nodes.baseline_x),
            baseline_y = COALESCE(excluded.baseline_y, nodes.baseline_y),
            baseline_z = COALESCE(excluded.baseline_z, nodes.baseline_z),
            notes = COALESCE(excluded.notes, nodes.notes)
        """, (node.node_id, node.name, node.lat, node.lon, node.elevation_m, node.baseline_x, node.baseline_y, node.baseline_z, node.notes))
        conn.commit()
    return {"status": "success", "node_id": node.node_id}

@app.get("/api/v1/data")
@app.get("/api/data")
def query_data(
    node_id: Optional[str] = Query(None, description="Filter by node ID"),
    start: Optional[str] = Query(None, description="Start timestamp (ISO format)"),
    end: Optional[str] = Query(None, description="End timestamp (ISO format)"),
    downsample_sec: Optional[int] = Query(None, description="Downsample averaging window in seconds (e.g. 60 for 1-min)"),
    format: str = Query("csv", description="Output format: csv, json, npz, or parquet")
):
    """Query time-series telemetry subsets and download in CSV, JSON, NumPy (.npz), or Parquet (.parquet)."""
    query = """
    SELECT timestamp, node_id, x, y, z, units, temp, vbat, rssi, status_flags
    FROM telemetry WHERE 1=1
    """
    params = []
    
    if node_id:
        query += " AND node_id = ?"
        params.append(node_id)
    if start:
        query += " AND timestamp >= ?"
        params.append(start)
    if end:
        query += " AND timestamp <= ?"
        params.append(end)
        
    query += " ORDER BY timestamp ASC"

    with get_db() as conn:
        cursor = conn.execute(query, params)
        rows = cursor.fetchall()

    if not rows:
        if format == "json":
            return {"schema_version": "1.0", "count": 0, "data": []}
        elif format == "csv":
            return Response(content="timestamp_utc,node_id,x_nT,y_nT,z_nT,magnitude_nT,temp_c,vbat_mv,rssi_dbm,status_flags\n", media_type="text/csv")

    data = [dict(r) for r in rows]
    df = pd.DataFrame(data)

    # Rename columns to standardized, self-describing field names
    df.rename(columns={
        "timestamp": "timestamp_utc",
        "x": "x_nT",
        "y": "y_nT",
        "z": "z_nT",
        "temp": "temp_c",
        "vbat": "vbat_mv",
        "rssi": "rssi_dbm"
    }, inplace=True)

    # Compute scalar total magnitude |B|
    df["magnitude_nT"] = np.sqrt(df["x_nT"]**2 + df["y_nT"]**2 + df["z_nT"]**2).round(2)

    # Apply optional time downsampling (averaging)
    if downsample_sec and downsample_sec > 1 and len(df) > 0:
        try:
            df["dt"] = pd.to_datetime(df["timestamp_utc"])
            agg_dict = {
                "x_nT": "mean",
                "y_nT": "mean",
                "z_nT": "mean",
                "magnitude_nT": "mean"
            }
            for col in ["temp_c", "vbat_mv", "rssi_dbm"]:
                if col in df.columns:
                    agg_dict[col] = "mean"
            if "status_flags" in df.columns:
                agg_dict["status_flags"] = "first"

            resampled = df.groupby(["node_id", pd.Grouper(key="dt", freq=f"{downsample_sec}s")]).agg(agg_dict).reset_index()
            resampled["timestamp_utc"] = resampled["dt"].dt.strftime("%Y-%m-%dT%H:%M:%SZ")
            resampled.drop(columns=["dt"], inplace=True)
            df = resampled
        except Exception as ds_err:
            logger.error(f"Downsampling error: {ds_err}")

    # Output Formats
    filename_base = f"mag_data_{node_id or 'all'}"

    if format == "csv":
        stream = io.StringIO()
        df.to_csv(stream, index=False)
        return Response(
            content=stream.getvalue(),
            media_type="text/csv",
            headers={"Content-Disposition": f"attachment; filename={filename_base}.csv"}
        )
    elif format == "npz":
        buf = io.BytesIO()
        np.savez_compressed(
            buf,
            timestamp=df["timestamp_utc"].to_numpy(dtype=str),
            node_id=df["node_id"].to_numpy(dtype=str),
            x_nT=df["x_nT"].to_numpy(dtype=np.float32),
            y_nT=df["y_nT"].to_numpy(dtype=np.float32),
            z_nT=df["z_nT"].to_numpy(dtype=np.float32),
            magnitude_nT=df["magnitude_nT"].to_numpy(dtype=np.float32),
            temp_c=df["temp_c"].to_numpy(dtype=np.float32),
            vbat_mv=df["vbat_mv"].to_numpy(dtype=np.float32),
            schema_version="1.0"
        )
        buf.seek(0)
        return StreamingResponse(
            buf,
            media_type="application/octet-stream",
            headers={"Content-Disposition": f"attachment; filename={filename_base}.npz"}
        )
    elif format == "parquet":
        try:
            buf = io.BytesIO()
            df.to_parquet(buf, index=False)
            buf.seek(0)
            return StreamingResponse(
                buf,
                media_type="application/octet-stream",
                headers={"Content-Disposition": f"attachment; filename={filename_base}.parquet"}
            )
        except Exception as e:
            logger.warning(f"Parquet export error: {e}")
            raise HTTPException(status_code=400, detail=f"Parquet export engine missing or failed: {str(e)}. Install pyarrow via 'pip install pyarrow'.")
    else:
        df_clean = df.where(pd.notnull(df), None)
        return {"schema_version": "1.0", "count": len(df_clean), "data": df_clean.to_dict(orient="records")}

@app.websocket("/ws/live")
async def websocket_endpoint(websocket: WebSocket):
    await ws_manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        ws_manager.disconnect(websocket)

if __name__ == "__main__":
    import uvicorn
    logger.info("=" * 60)
    logger.info("  Magnetometer Central Data Server")
    logger.info("=" * 60)
    logger.info(f"  Listening on:   http://{HOST}:{PORT}")
    logger.info(f"  Web Dashboard:  http://{HOST}:{PORT}/")
    logger.info(f"  API Docs:       http://{HOST}:{PORT}/docs")
    logger.info(f"  Database Path:  {os.path.abspath(DB_FILE)}")
    logger.info(f"  Security:       {'API Key Authentication ENABLED' if API_KEY else 'Open Access (Local / Dev mode)'}")
    logger.info("=" * 60)
    uvicorn.run("server:app", host=HOST, port=PORT, reload=False)
