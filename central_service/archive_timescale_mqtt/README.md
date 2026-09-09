# TimescaleDB & MQTT Enterprise Backend Archive

This directory contains experimental / reference implementations for high-density, multi-node enterprise deployments:

- **`app/`**: Async SQLAlchemy FastAPI module designed for PostgreSQL with **TimescaleDB** time-series hypertables and **PostGIS** spatial event correlation (`ST_DWithin`).
- **`mosquitto/`**: Configuration files for an external Mosquitto MQTT message broker.

### Primary Central Server
For standalone, edge, and field deployments, the project uses the self-contained SQLite FastAPI backend:
- Main entry point: `central_service/server.py`
- Control script: `central_service/manage.sh`
