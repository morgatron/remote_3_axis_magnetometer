# Overview

This repository contains firmware, an edge gateway, a PySide6 desktop visualization application, and a lightweight central data server (`central_service`) for an ESP32-based 3-axis magnetometer acquisition system.

A single universal firmware binary supports two sensor configurations via NVS dynamic provisioning:
1. **PNI RM3100**: High-resolution digital SPI magnetometer (Hardware REVID `0x22`).
2. **FLC-100 Analog Fluxgate Array**: Differential analog front-end sampled via an external **TI ADS131E08 24-bit 8-channel SPI ADC** (VREF = 2.4V, 2.0 MHz SPI clock).

The system streams calibrated 6-column magnetic field data in **Nanotesla (nT)** over Serial (921.6 kbaud), WiFi UDP (Port 9876), or Bluetooth 5.0 LE (Nordic UART Service) at up to **1 kSPS** (600 Hz max for RM3100).

---

## Key System Architecture Features

1. **MCU On-Board Scaling**:
   - The ESP32 calculates scale factors on-board: $\text{Gain}(N_C) = 0.3671 \times N_C + 1.5\ \text{LSB}/\mu\text{T}$.
   - Converting raw counts directly to physical Nanotesla ($\text{nT}$) before output ensures the reported magnitude $|\mathbf{B}|$ remains invariant across dynamic cycle count ($N_C \in [50, 400]$) and rate changes.

2. **Self-Healing Hardware Watchdog**:
   - Detects SPI bus stalls or missed `/DRDY` interrupts (e.g. from physical bumps or power glitches) within **500 ms**.
   - Automatically detaches interrupts, resets SPI CS pins, re-probes and re-initializes the sensor ASIC, clears hardware latches, and resumes streaming without requiring a manual reboot.

3. **Central Data Server (`central_service/`)**:
   - Single-file FastAPI + SQLite (WAL mode) time-series server designed for 10–50 distributed nodes on a Raspberry Pi (< 30 MB RAM).
   - Real-time Web GUI (`http://localhost:8000`) with live WebSockets (`/ws/live`).
   - Versioned API (`/api/v1/...`) with export support for **CSV**, **Apache Parquet (`.parquet`)**, **NumPy (`.npz`)**, and **JSON**.
   - Built-in server-side downsampling (`downsample_sec=60`) for multi-month trend analysis.

4. **Unified Edge Gateway (`central_service/gateway.py`)**:
   - Listens concurrently for WiFi UDP (port 9876), BLE 5.0 (Nordic UART Service), and USB Serial streams.
   - Includes a thread-safe **Store-and-Forward Buffer** to queue telemetry during network outages and flush batch payloads upon reconnection.

5. **PySide6 Desktop Application (`desktop_app/main.py`)**:
   - Provides real-time multi-axis time-series plotting, Welch PSD spectral analysis, device provisioning, and Gzip-compressed HDF5 (`.h5`) logging.

---

## Hardware Pinouts & Auto-Detection

### Pinout Reference

| Signal | ESP32 Dev Module (`esp32dev`) | ESP32-C3 PCB (`esp32-c3-devkitm-1`) |
| :--- | :--- | :--- |
| **SCK** | GPIO 18 | GPIO 6 |
| **MISO** | GPIO 19 | GPIO 2 |
| **MOSI** | GPIO 23 | GPIO 7 |
| **CS** | GPIO 5 | GPIO 10 |
| **DRDY** | GPIO 4 | GPIO 3 (Hardware interrupt) |
| **Port** | `/dev/ttyUSB0` | `/dev/ttyACM0` |

---

## Data Stream Format & CLI Reference

### 6-Column CSV Data Stream
`device_id,timestamp_us,x_nT,y_nT,z_nT,status`
- **`device_id`**: Node identifier string (e.g. `SENSOR_01` or MAC-derived `NODE_686F80`).
- **`timestamp_us`**: MCU uptime in microseconds (`uint64_t`).
- **`x_nT, y_nT, z_nT`**: Calibrated 3-axis magnetic field in Nanotesla ($\text{nT}$).
- **`status`**: 24-bit hex SPI status word (e.g., `C00000`).

### Interactive CLI Commands (Serial / UDP / BLE)

| Command | Description |
| :--- | :--- |
| `HELP` / `STATUS` | Display CLI help / Query Device ID, sensor model, rate code, and status |
| `STREAM ON` / `OFF` | Enable / disable continuous telemetry stream |
| `ID <name>` | Configure custom Device ID / Node name (saves to NVS Flash) |
| `SENSOR <FLC100\|RM3100>` | Force active sensor model and reboot |
| `RATE <hex>` | Set rate code (`0x95` = 75 Hz, `0x94` = 150 Hz, `0x93` = 300 Hz, `0x92` = 600 Hz) |
| `CYCLE <int>` | Set RM3100 oscillation cycle count (e.g. `CYCLE 200`) |
| `MODE <SERIAL\|WIFI\|BLE\|BOTH>` | Route stream to USB Serial, WiFi UDP, BLE Long Range, or both |

---

## Running & Testing

### Conda Environment Setup
```bash
conda env create -f environment.yml
conda activate rm3100
```

### Firmware Build & Flash
```bash
# Classic ESP32 Dev Module
pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0

# ESP32-C3 PCB
pio run -e esp32-c3-devkitm-1 -t upload --upload-port /dev/ttyACM0
```

### Central Data Server & Web GUI
```bash
cd central_service
python server.py
# Open http://localhost:8000
```

### Run Automated Test Suite
```bash
# 1. Math Invariance Unit Test
python test/test_scaling_math.py

# 2. Central Server API & Export Test
python central_service/test_server.py

# 3. Hardware Scaling Invariance Test (Requires connected ESP32)
python test_cycle_count_invariance.py --port /dev/ttyUSB0
```
