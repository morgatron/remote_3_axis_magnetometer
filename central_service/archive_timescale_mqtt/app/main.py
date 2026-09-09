import math
import logging
import asyncio
from typing import List, Optional
from datetime import datetime, timezone
from contextlib import asynccontextmanager

from fastapi import FastAPI, Depends, HTTPException, BackgroundTask, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.sql import text

from app.schemas import (
    TelemetryPayload,
    BatchTelemetryPayload,
    NodeRegister,
    NodeResponse,
    RegionalEventQuery
)
from app.database import get_db, init_db, AsyncSessionLocal

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("central_service")

# Simple WebSocket Connection Manager for Live Dashboard Streaming
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

@asynccontextmanager
async def lifespan(app: FastAPI):
    # Startup logic
    logger.info("Starting Central Magnetometer Service...")
    await init_db()
    yield
    # Shutdown logic
    logger.info("Shutting down Central Service...")

app = FastAPI(
    title="Distributed Magnetometer Central Service",
    description="Central ingestion and regional magnetic event analysis API for distributed 3-axis magnetometers.",
    version="1.0.0",
    lifespan=lifespan
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.get("/health")
async def health_check():
    return {"status": "ok", "timestamp": datetime.now(timezone.utc).isoformat()}

async def save_telemetry_sample(db: AsyncSession, payload: TelemetryPayload):
    ts = payload.timestamp_utc or datetime.now(timezone.utc)
    bx = payload.magnetic.bx_nT
    by = payload.magnetic.by_nT
    bz = payload.magnetic.bz_nT
    mag = payload.magnetic.magnitude_nT
    if mag is None:
        mag = math.sqrt(bx*bx + by*by + bz*bz)
    
    temp_c = payload.diagnostics.temp_c if payload.diagnostics else None
    vbat_mv = payload.diagnostics.vbat_mv if payload.diagnostics else None
    rssi_dbm = payload.diagnostics.rssi_dbm if payload.diagnostics else None
    status_flags = payload.diagnostics.sensor_status if payload.diagnostics else None

    # Upsert node registration placeholder if node doesn't exist yet
    upsert_node_sql = text("""
    INSERT INTO sensor_nodes (node_id, name, sensor_model, location)
    VALUES (:node_id, :name, 'RM3100', ST_SetSRID(ST_MakePoint(0.0, 0.0), 4326))
    ON CONFLICT (node_id) DO NOTHING;
    """)
    await db.execute(upsert_node_sql, {"node_id": payload.node_id, "name": f"Node {payload.node_id}"})

    # Update node location if dynamic payload has geo coordinates
    if payload.geo:
        update_geo_sql = text("""
        UPDATE sensor_nodes
        SET location = ST_SetSRID(ST_MakePoint(:lon, :lat), 4326),
            elevation_m = :alt
        WHERE node_id = :node_id;
        """)
        await db.execute(update_geo_sql, {
            "node_id": payload.node_id,
            "lon": payload.geo.lon,
            "lat": payload.geo.lat,
            "alt": payload.geo.alt_m or 0.0
        })

    # Insert Telemetry Record
    insert_sql = text("""
    INSERT INTO mag_telemetry (time, node_id, bx_nT, by_nT, bz_nT, magnitude_nT, temp_c, vbat_mv, rssi_dbm, status_flags)
    VALUES (:time, :node_id, :bx, :by, :bz, :mag, :temp_c, :vbat_mv, :rssi_dbm, :status_flags);
    """)
    await db.execute(insert_sql, {
        "time": ts,
        "node_id": payload.node_id,
        "bx": bx,
        "by": by,
        "bz": bz,
        "mag": mag,
        "temp_c": temp_c,
        "vbat_mv": vbat_mv,
        "rssi_dbm": rssi_dbm,
        "status_flags": status_flags
    })
    await db.commit()

    # Broadcast to live WebSockets
    asyncio.create_task(ws_manager.broadcast({
        "type": "telemetry",
        "node_id": payload.node_id,
        "timestamp": ts.isoformat(),
        "bx": bx,
        "by": by,
        "bz": bz,
        "magnitude": mag
    }))

@app.post("/api/v1/telemetry", status_code=201)
async def ingest_telemetry(payload: TelemetryPayload, db: AsyncSession = Depends(get_db)):
    """Single telemetry sample HTTP POST ingestion endpoint (1Hz or periodic)."""
    try:
        await save_telemetry_sample(db, payload)
        return {"status": "success", "node_id": payload.node_id}
    except Exception as e:
        logger.error(f"Failed to save telemetry: {e}")
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/telemetry/batch", status_code=201)
async def ingest_telemetry_batch(batch: BatchTelemetryPayload, db: AsyncSession = Depends(get_db)):
    """Batch telemetry sample HTTP POST ingestion endpoint for store-and-forward recovery."""
    try:
        count = 0
        for sample in batch.samples:
            sample.node_id = batch.node_id
            await save_telemetry_sample(db, sample)
            count += 1
        return {"status": "success", "node_id": batch.node_id, "samples_ingested": count}
    except Exception as e:
        logger.error(f"Failed batch ingest: {e}")
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/nodes/register", response_model=NodeResponse)
async def register_node(node_data: NodeRegister, db: AsyncSession = Depends(get_db)):
    """Register or update static node details and spatial coordinates."""
    sql = text("""
    INSERT INTO sensor_nodes (
        node_id, name, sensor_model, location, elevation_m,
        baseline_bx_nT, baseline_by_nT, baseline_bz_nT
    )
    VALUES (
        :node_id, :name, :sensor_model, ST_SetSRID(ST_MakePoint(:lon, :lat), 4326), :elevation_m,
        :bx, :by, :bz
    )
    ON CONFLICT (node_id) DO UPDATE SET
        name = EXCLUDED.name,
        sensor_model = EXCLUDED.sensor_model,
        location = EXCLUDED.location,
        elevation_m = EXCLUDED.elevation_m,
        baseline_bx_nT = EXCLUDED.baseline_bx_nT,
        baseline_by_nT = EXCLUDED.baseline_by_nT,
        baseline_bz_nT = EXCLUDED.baseline_bz_nT
    RETURNING node_id, name, sensor_model, ST_Y(location) as lat, ST_X(location) as lon, elevation_m, is_active, installation_date;
    """)
    result = await db.execute(sql, {
        "node_id": node_data.node_id,
        "name": node_data.name,
        "sensor_model": node_data.sensor_model,
        "lat": node_data.lat,
        "lon": node_data.lon,
        "elevation_m": node_data.elevation_m,
        "bx": node_data.baseline_bx_nT,
        "by": node_data.baseline_by_nT,
        "bz": node_data.baseline_bz_nT
    })
    await db.commit()
    row = result.fetchone()
    if not row:
        raise HTTPException(status_code=400, detail="Failed to register node")
    return NodeResponse(
        node_id=row.node_id,
        name=row.name,
        sensor_model=row.sensor_model,
        lat=row.lat,
        lon=row.lon,
        elevation_m=row.elevation_m,
        is_active=row.is_active,
        installation_date=row.installation_date
    )

@app.get("/api/v1/nodes", response_model=List[NodeResponse])
async def list_nodes(db: AsyncSession = Depends(get_db)):
    """List all registered magnetometer nodes."""
    sql = text("""
    SELECT node_id, name, sensor_model, ST_Y(location) as lat, ST_X(location) as lon, elevation_m, is_active, installation_date
    FROM sensor_nodes
    ORDER BY name ASC;
    """)
    result = await db.execute(sql)
    rows = result.fetchall()
    return [
        NodeResponse(
            node_id=r.node_id,
            name=r.name,
            sensor_model=r.sensor_model,
            lat=r.lat,
            lon=r.lon,
            elevation_m=r.elevation_m,
            is_active=r.is_active,
            installation_date=r.installation_date
        ) for r in rows
    ]

@app.get("/api/v1/telemetry/latest")
async def get_latest_vectors(db: AsyncSession = Depends(get_db)):
    """Fetch the latest magnetic vector reading for every node in the network."""
    sql = text("""
    SELECT DISTINCT ON (t.node_id)
        t.node_id, n.name, ST_Y(n.location) as lat, ST_X(n.location) as lon,
        t.time, t.bx_nT, t.by_nT, t.bz_nT, t.magnitude_nT,
        n.baseline_bx_nT, n.baseline_by_nT, n.baseline_bz_nT
    FROM mag_telemetry t
    JOIN sensor_nodes n ON t.node_id = n.node_id
    ORDER BY t.node_id, t.time DESC;
    """)
    result = await db.execute(sql)
    rows = result.fetchall()
    
    response_nodes = []
    for r in rows:
        # Subtract quiet baseline to get anomaly vector delta
        delta_bx = r.bx_nT - (r.baseline_bx_nT or 0.0)
        delta_by = r.by_nT - (r.baseline_by_nT or 0.0)
        delta_bz = r.bz_nT - (r.baseline_bz_nT or 0.0)
        delta_mag = math.sqrt(delta_bx*delta_bx + delta_by*delta_by + delta_bz*delta_bz)
        
        response_nodes.append({
            "node_id": r.node_id,
            "name": r.name,
            "location": {"lat": r.lat, "lon": r.lon},
            "timestamp": r.time.isoformat(),
            "raw_vector_nT": {"bx": r.bx_nT, "by": r.by_nT, "bz": r.bz_nT, "magnitude": r.magnitude_nT},
            "anomaly_vector_nT": {"delta_bx": delta_bx, "delta_by": delta_by, "delta_bz": delta_bz, "magnitude": delta_mag}
        })
    return {"nodes": response_nodes}

@app.post("/api/v1/analytics/regional_event")
async def analyze_regional_event(query: RegionalEventQuery, db: AsyncSession = Depends(get_db)):
    """Query time-series telemetry across multiple nodes to analyze spatial-temporal event responses."""
    sql = text("""
    SELECT t.time, t.node_id, n.name, ST_Y(n.location) as lat, ST_X(n.location) as lon,
           t.bx_nT, t.by_nT, t.bz_nT, t.magnitude_nT
    FROM mag_telemetry t
    JOIN sensor_nodes n ON t.node_id = n.node_id
    WHERE t.time >= :start_time AND t.time <= :end_time
      AND (:min_lat IS NULL OR ST_Y(n.location) >= :min_lat)
      AND (:max_lat IS NULL OR ST_Y(n.location) <= :max_lat)
      AND (:min_lon IS NULL OR ST_X(n.location) >= :min_lon)
      AND (:max_lon IS NULL OR ST_X(n.location) <= :max_lon)
    ORDER BY t.time ASC, t.node_id ASC;
    """)
    result = await db.execute(sql, {
        "start_time": query.start_time,
        "end_time": query.end_time,
        "min_lat": query.min_lat,
        "max_lat": query.max_lat,
        "min_lon": query.min_lon,
        "max_lon": query.max_lon
    })
    rows = result.fetchall()
    
    samples = [
        {
            "timestamp": r.time.isoformat(),
            "node_id": r.node_id,
            "name": r.name,
            "lat": r.lat,
            "lon": r.lon,
            "bx_nT": r.bx_nT,
            "by_nT": r.by_nT,
            "bz_nT": r.bz_nT,
            "magnitude_nT": r.magnitude_nT
        } for r in rows
    ]
    return {"total_samples": len(samples), "data": samples}

@app.websocket("/ws/live")
async def websocket_live_stream(websocket: WebSocket):
    await ws_manager.connect(websocket)
    try:
        while True:
            # Keep socket alive
            await websocket.receive_text()
    except WebSocketDisconnect:
        ws_manager.disconnect(websocket)
