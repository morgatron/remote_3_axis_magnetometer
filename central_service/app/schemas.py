from datetime import datetime
from typing import Optional, List
from pydantic import BaseModel, Field, ConfigDict

class GeoPoint(BaseModel):
    lat: float = Field(..., description="Latitude in decimal degrees", ge=-90.0, le=90.0)
    lon: float = Field(..., description="Longitude in decimal degrees", ge=-180.0, le=180.0)
    alt_m: Optional[float] = Field(None, description="Altitude in meters above sea level")

class MagneticData(BaseModel):
    bx_nT: float = Field(..., description="X-axis magnetic field component in nT")
    by_nT: float = Field(..., description="Y-axis magnetic field component in nT")
    bz_nT: float = Field(..., description="Z-axis magnetic field component in nT")
    magnitude_nT: Optional[float] = Field(None, description="Total scalar field magnitude in nT")

class Diagnostics(BaseModel):
    temp_c: Optional[float] = Field(None, description="Sensor / MCU temperature in Celsius")
    vbat_mv: Optional[int] = Field(None, description="Battery voltage in millivolts")
    rssi_dbm: Optional[int] = Field(None, description="WiFi / Cellular RSSI in dBm")
    sensor_status: Optional[str] = Field("0xC00000", description="24-bit sensor status flags in hex")

class TelemetryPayload(BaseModel):
    node_id: str = Field(..., description="Unique node identifier string, e.g., MAG_NORTH_01")
    timestamp_utc: Optional[datetime] = Field(default_factory=datetime.utcnow, description="UTC ISO8601 sample timestamp")
    uptime_ms: Optional[int] = Field(None, description="Node uptime in milliseconds")
    geo: Optional[GeoPoint] = Field(None, description="Dynamic GPS position if equipped")
    magnetic: MagneticData
    diagnostics: Optional[Diagnostics] = None

class BatchTelemetryPayload(BaseModel):
    node_id: str
    samples: List[TelemetryPayload]

class NodeRegister(BaseModel):
    node_id: str = Field(..., description="Unique hardware ID")
    name: str = Field(..., description="Human-readable node location/name")
    sensor_model: str = Field("RM3100", description="Sensor model: RM3100 or FLC100")
    lat: float
    lon: float
    elevation_m: float = 0.0
    baseline_bx_nT: float = 0.0
    baseline_by_nT: float = 0.0
    baseline_bz_nT: float = 0.0

class NodeResponse(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    node_id: str
    name: str
    sensor_model: str
    lat: float
    lon: float
    elevation_m: float
    is_active: bool
    installation_date: datetime

class RegionalEventQuery(BaseModel):
    start_time: datetime
    end_time: datetime
    min_lat: Optional[float] = None
    max_lat: Optional[float] = None
    min_lon: Optional[float] = None
    max_lon: Optional[float] = None
    node_ids: Optional[List[str]] = None
