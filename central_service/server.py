import sqlite3
import os
import io
import asyncio
import json
import math
from datetime import datetime, timezone
from typing import Optional, List

from fastapi import FastAPI, HTTPException, Query, Response, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
import numpy as np
import pandas as pd

DB_FILE = os.getenv("DB_FILE", "magnetometer.db")

def get_db():
    conn = sqlite3.connect(DB_FILE)
    conn.row_factory = sqlite3.Row
    return conn

def init_db():
    with get_db() as conn:
        conn.execute("PRAGMA journal_mode=WAL;")
        conn.execute("""
        CREATE TABLE IF NOT EXISTS telemetry (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            node_id TEXT NOT NULL,
            x REAL NOT NULL,
            y REAL NOT NULL,
            z REAL NOT NULL,
            temp REAL,
            vbat INTEGER
        );
        """)
        conn.execute("CREATE INDEX IF NOT EXISTS idx_telemetry_node_time ON telemetry(node_id, timestamp);")
        conn.execute("""
        CREATE TABLE IF NOT EXISTS nodes (
            node_id TEXT PRIMARY KEY,
            name TEXT,
            lat REAL,
            lon REAL,
            last_seen TEXT
        );
        """)
        conn.commit()

init_db()

# WebSocket Connection Manager for Real-Time GUI Updates
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
    title="Lightweight Magnetometer Central Data Server",
    description="Simple, lightweight server for 10-50 magnetometer nodes with real-time web monitoring."
)

# Serve static files for web GUI
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

class TelemetryPoint(BaseModel):
    node_id: str
    timestamp: Optional[str] = None
    x: float
    y: float
    z: float
    temp: Optional[float] = None
    vbat: Optional[int] = None
    lat: Optional[float] = None
    lon: Optional[float] = None

class BatchTelemetry(BaseModel):
    node_id: str
    points: List[TelemetryPoint]

@app.get("/health")
def health():
    return {"status": "ok", "db": DB_FILE}

@app.post("/api/telemetry", status_code=201)
async def ingest_sample(point: TelemetryPoint):
    """Ingest a single reading from a node (HTTP POST)."""
    ts = point.timestamp or datetime.now(timezone.utc).isoformat()
    with get_db() as conn:
        conn.execute(
            "INSERT INTO telemetry (timestamp, node_id, x, y, z, temp, vbat) VALUES (?, ?, ?, ?, ?, ?, ?)",
            (ts, point.node_id, point.x, point.y, point.z, point.temp, point.vbat)
        )
        conn.execute(
            "INSERT INTO nodes (node_id, name, lat, lon, last_seen) VALUES (?, ?, ?, ?, ?) "
            "ON CONFLICT(node_id) DO UPDATE SET last_seen=excluded.last_seen, "
            "lat=COALESCE(excluded.lat, nodes.lat), lon=COALESCE(excluded.lon, nodes.lon)",
            (point.node_id, point.node_id, point.lat, point.lon, ts)
        )
        conn.commit()

    # Broadcast live reading to Web GUI
    await ws_manager.broadcast({
        "type": "telemetry",
        "node_id": point.node_id,
        "timestamp": ts,
        "x": point.x,
        "y": point.y,
        "z": point.z,
        "temp": point.temp,
        "vbat": point.vbat
    })
    return {"status": "ok"}

@app.post("/api/telemetry/batch", status_code=201)
async def ingest_batch(batch: BatchTelemetry):
    """Ingest a batch of readings from a node."""
    rows = []
    now_str = datetime.now(timezone.utc).isoformat()
    for p in batch.points:
        ts = p.timestamp or now_str
        rows.append((ts, batch.node_id, p.x, p.y, p.z, p.temp, p.vbat))
    
    with get_db() as conn:
        conn.executemany(
            "INSERT INTO telemetry (timestamp, node_id, x, y, z, temp, vbat) VALUES (?, ?, ?, ?, ?, ?, ?)",
            rows
        )
        conn.execute(
            "INSERT INTO nodes (node_id, name, last_seen) VALUES (?, ?, ?) "
            "ON CONFLICT(node_id) DO UPDATE SET last_seen=excluded.last_seen",
            (batch.node_id, batch.node_id, now_str)
        )
        conn.commit()

    # Broadcast last point in batch
    if batch.points:
        last_pt = batch.points[-1]
        await ws_manager.broadcast({
            "type": "telemetry",
            "node_id": batch.node_id,
            "timestamp": last_pt.timestamp or now_str,
            "x": last_pt.x,
            "y": last_pt.y,
            "z": last_pt.z,
            "temp": last_pt.temp,
            "vbat": last_pt.vbat
        })
    return {"status": "ok", "inserted": len(rows)}

@app.get("/api/nodes")
def list_nodes():
    """List all reporting nodes and last seen timestamp."""
    with get_db() as conn:
        cursor = conn.execute("""
        SELECT n.node_id, n.name, n.lat, n.lon, n.last_seen,
               t.x, t.y, t.z, t.temp, t.vbat
        FROM nodes n
        LEFT JOIN telemetry t ON t.id = (
            SELECT id FROM telemetry WHERE node_id = n.node_id ORDER BY timestamp DESC LIMIT 1
        )
        ORDER BY n.node_id;
        """)
        return [dict(row) for row in cursor.fetchall()]

@app.get("/api/data")
def query_data(
    node_id: Optional[str] = Query(None, description="Filter by node ID"),
    start: Optional[str] = Query(None, description="Start timestamp (ISO format)"),
    end: Optional[str] = Query(None, description="End timestamp (ISO format)"),
    format: str = Query("csv", description="Output format: csv, json, or npz (NumPy compressed array)")
):
    """Query data subsets and download directly as CSV, JSON, or NumPy (.npz)."""
    query = "SELECT timestamp, node_id, x, y, z, temp, vbat FROM telemetry WHERE 1=1"
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
            return []
        elif format == "csv":
            return Response(content="timestamp,node_id,x,y,z,temp,vbat\n", media_type="text/csv")

    data = [dict(r) for r in rows]
    df = pd.DataFrame(data)

    if format == "csv":
        stream = io.StringIO()
        df.to_csv(stream, index=False)
        filename = f"mag_data_{node_id or 'all'}.csv"
        return Response(
            content=stream.getvalue(),
            media_type="text/csv",
            headers={"Content-Disposition": f"attachment; filename={filename}"}
        )
    elif format == "npz":
        buf = io.BytesIO()
        np.savez_compressed(
            buf,
            timestamp=df["timestamp"].to_numpy(dtype=str),
            node_id=df["node_id"].to_numpy(dtype=str),
            x=df["x"].to_numpy(dtype=np.float32),
            y=df["y"].to_numpy(dtype=np.float32),
            z=df["z"].to_numpy(dtype=np.float32),
            temp=df["temp"].to_numpy(dtype=np.float32),
            vbat=df["vbat"].to_numpy(dtype=np.int32)
        )
        buf.seek(0)
        filename = f"mag_data_{node_id or 'all'}.npz"
        return StreamingResponse(
            buf,
            media_type="application/octet-stream",
            headers={"Content-Disposition": f"attachment; filename={filename}"}
        )
    else:
        return data

@app.websocket("/ws/live")
async def websocket_endpoint(websocket: WebSocket):
    await ws_manager.connect(websocket)
    try:
        while True:
            # Keep socket alive
            await websocket.receive_text()
    except WebSocketDisconnect:
        ws_manager.disconnect(websocket)

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("server:app", host="0.0.0.0", port=8000, reload=True)
