import os
import logging
from sqlalchemy.ext.asyncio import create_async_engine, AsyncSession, async_sessionmaker
from sqlalchemy.sql import text

logger = logging.getLogger(__name__)

DB_USER = os.getenv("POSTGRES_USER", "postgres")
DB_PASS = os.getenv("POSTGRES_PASSWORD", "postgres")
DB_HOST = os.getenv("POSTGRES_HOST", "db")
DB_PORT = os.getenv("POSTGRES_PORT", "5432")
DB_NAME = os.getenv("POSTGRES_DB", "magnetometer_db")

DATABASE_URL = f"postgresql+asyncpg://{DB_USER}:{DB_PASS}@{DB_HOST}:{DB_PORT}/{DB_NAME}"

engine = create_async_engine(DATABASE_URL, echo=False, pool_pre_ping=True)
AsyncSessionLocal = async_sessionmaker(engine, class_=AsyncSession, expire_on_commit=False)

async def get_db():
    async with AsyncSessionLocal() as session:
        try:
            yield session
        finally:
            await session.close()

INIT_SQL = """
-- 1. Enable Extensions
CREATE EXTENSION IF NOT EXISTS timescaledb CASCADE;
CREATE EXTENSION IF NOT EXISTS postgis CASCADE;

-- 2. Metadata Registry for Nodes
CREATE TABLE IF NOT EXISTS sensor_nodes (
    node_id VARCHAR(64) PRIMARY KEY,
    name VARCHAR(128) NOT NULL,
    sensor_model VARCHAR(32) NOT NULL DEFAULT 'RM3100',
    location GEOMETRY(Point, 4326),
    elevation_m REAL DEFAULT 0.0,
    installation_date TIMESTAMPTZ DEFAULT NOW(),
    is_active BOOLEAN DEFAULT TRUE,
    baseline_bx_nT REAL DEFAULT 0.0,
    baseline_by_nT REAL DEFAULT 0.0,
    baseline_bz_nT REAL DEFAULT 0.0
);

-- 3. Hypertable for Magnetometer Telemetry
CREATE TABLE IF NOT EXISTS mag_telemetry (
    time TIMESTAMPTZ NOT NULL,
    node_id VARCHAR(64) NOT NULL,
    bx_nT REAL NOT NULL,
    by_nT REAL NOT NULL,
    bz_nT REAL NOT NULL,
    magnitude_nT REAL,
    temp_c REAL,
    vbat_mv INT,
    rssi_dbm INT,
    status_flags VARCHAR(16)
);

-- Convert to Hypertable if not already converted
SELECT create_hypertable('mag_telemetry', 'time', if_not_exists => TRUE);

-- Create Indexes for fast querying
CREATE INDEX IF NOT EXISTS idx_mag_telemetry_node_time ON mag_telemetry (node_id, time DESC);
CREATE INDEX IF NOT EXISTS idx_sensor_nodes_location ON sensor_nodes USING GIST (location);
"""

async def init_db():
    logger.info("Initializing TimescaleDB & PostGIS schemas...")
    async with engine.begin() as conn:
        # Split statements and execute individually if required by asyncpg
        for statement in INIT_SQL.strip().split(";"):
            stmt = statement.strip()
            if stmt:
                await conn.execute(text(stmt))
    logger.info("Database schemas initialized successfully.")
