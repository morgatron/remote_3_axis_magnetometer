# Hardware Diagnostic & Manual Test Scripts

This directory contains standalone, manual hardware test scripts and diagnostics designed to be run against physically connected ESP32 boards via USB serial or local network interfaces.

Unlike unit and host integration tests in `test/`, these scripts are not run as part of automated CI/test discovery.

---

## Script Index

| Script | Interface | Target / Description |
| :--- | :--- | :--- |
| `test_hardware.py` | Serial (`/dev/ttyUSB*`, `/dev/ttyACM*`) | Pytest-based interactive test verifying CLI commands (`HELP`, `STATUS`, `STREAM ON/OFF`) and CSV stream format. Requires `pytest`. |
| `test_raw_spikes.py` | Serial (`/dev/ttyUSB0`, 921600 baud) | Monitors raw ASCII magnetometer stream to detect single-axis sudden deltas/spikes (>40 nT). |
| `test_wifi_connect.py` | Serial (`/dev/ttyUSB0`, 921600 baud) | Sends Wi-Fi connection and target IP configuration commands to an attached gateway node. |
| `test_wifi_provision.py` | Serial (`/dev/ttyUSB0`, 921600 baud) | Queries Wi-Fi status and system status from an attached gateway node. |
| `test_udp_listen.py` | Network (UDP port 9876) | Binds to local UDP port 9876 and logs received magnetometer telemetry packets broadcast over Wi-Fi. |
