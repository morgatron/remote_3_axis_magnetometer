# Overview

This repository contains firmware and a PySide6 desktop visualization application for an ESP32-based 3-axis magnetometer acquisition system. A single universal firmware binary supports two sensor configurations via NVS dynamic provisioning:

1. **FLC-100 Analog Fluxgate Array**: Differential analog front-end sampled via an external **TI ADS131E08 24-bit 8-channel SPI ADC** (VREF = 2.4V, 2.0 MHz SPI clock).
2. **PNI RM3100**: High-resolution digital SPI magnetometer (Hardware REVID `0x22`).

Streams 5-column magnetic field data over Serial (921.6 kbaud) or WiFi UDP (Port 9876) at up to **1 kSPS** (600 Hz max for RM3100). The companion PySide6 desktop application (`desktop_app`) provides real-time time-series plotting, automated Welch Power Spectral Density (PSD) analysis, and Gzip-compressed **HDF5 (`.h5`)** scientific logging with embedded calibration metadata.

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

### Auto-Detection Sequence
During `setup()`, the MCU probes SPI devices in sequence:
1. **RM3100**: Reads Revision ID register `0x36`. If `REVID == 0x22`, locks to RM3100.
2. **FLC100-ADS131E08**: Reads `ADS131_REG_ID` (`0x00`). If lower nibble `== 0x02`, locks to FLC100.
3. Upon connection in **Auto-Detect** mode, the GUI receives the sensor status line, updates the toolbar dropdown to match, populates available sampling rates, and activates the appropriate sidebar controls.

---

## Sensor Mechanics & Scaling Formulas

### PNI RM3100 Digital Magnetometer
- **Physical Sensitivity Gain**:
  $$\text{Gain}(N_C) = (0.3671 \times N_C + 1.5)\ \text{LSB}/\mu\text{T}$$
- **Nanotesla (nT) Conversion Factor**:
  $$\text{Scale Factor (nT per count)} = \frac{1000.0}{\text{Gain}(N_C)}$$
  *Dividing raw 24-bit integer counts directly by $\text{Gain}(N_C)$ yields an invariant Earth field reading ($\approx 46,000\text{ nT}$) across all cycle count settings ($50$, $100$, $200$, $400$).*
- **High-Speed Rate Capping**: To keep physical 3-axis measurement durations ($T_{\text{meas}}$) within sampling rate intervals, cycle counts are automatically capped:
  - **150 Hz (`0x94`)**: $N_C \le 100$
  - **300 Hz (`0x93`)**: $N_C \le 50$
  - **600 Hz (`0x92`)**: $N_C \le 30$
- **Live Register Updates (CMM Pause)**: Per RM3100 manual Section 5.2, `RM3100::setCycleCount` temporarily pauses Continuous Measurement Mode (`CMM = 0x00`), burst-writes the 6 cycle count registers (`0x04`..`0x09`), and restarts `CMM`, enabling live adjustments during active streaming.

### FLC100 + TI ADS131E08 24-bit ADC
- **Status Header**: 24-bit status header contains ADC channel fault and lead-off flags. Headers are verified by checking that SPI MISO is active (not all `0x00` or `0xFF`).
- **Raw ADC Scaling**: Raw 24-bit signed counts reflect direct voltage output ($1\text{ mV/}\mu\text{T}$).

---

## Data Stream Format & CLI Reference

### 5-Column CSV Data Stream
`timestamp_us,x,y,z,status`
- **`timestamp_us`**: Uptime in microseconds (`uint64_t`).
- **`x, y, z`**: Magnetic field values in nT (RM3100) or raw ADC counts (FLC100).
- **`status`**: 24-bit hex SPI status word (e.g., `C00000`).

### Interactive CLI Commands (Serial / UDP 9876)

| Command | Description |
| :--- | :--- |
| `HELP` / `STATUS` | Display CLI help / Query sensor model, rate code, and register status |
| `STREAM ON` / `OFF` | Enable / disable continuous sampling data stream |
| `SENSOR <FLC100\|RM3100>` | Force active sensor model (saves to NVS Flash and reboots MCU) |
| `RATE <hex>` | Set rate code (`0x06` for 1 kSPS FLC100, `0x95` for 75 Hz RM3100, `0x92` for 600 Hz RM3100) |
| `CYCLE <int>` | Set RM3100 oscillation cycle count (e.g. `CYCLE 200`) |
| `DOWNSAMPLE <int>` | Set software decimation factor for FLC100 (e.g., `1` = 1 kSPS, `10` = 100 Hz) |
| `GAIN <int>` | Set ADS131E08 PGA gain (`1`, `2`, `4`, `8`) |
| `MODE <SERIAL\|WIFI\|BOTH>` | Direct output stream to Serial (USB), WiFi UDP, or both |
| `WIFI <ssid> <pass>` | Configure WiFi station mode credentials |
| `TARGET <ip>` | Set destination server IP for UDP packet bursts |

---

## Desktop Application & HDF5 Logging

The PySide6 desktop GUI (`desktop_app/main.py`) provides real-time time-series plotting, PSD analysis, node provisioning, and HDF5 logging:

### Embedded HDF5 Root Metadata (`f.attrs`)
Every recorded `.h5` file automatically embeds complete acquisition metadata:
- `sensor_type`: `"RM3100"` or `"FLC100-ADS131E08"`
- `rate_code_hex` & `rate_code_dec`: Selected rate code
- `cycle_count_spinbox` & `cycle_count_active`: Configured cycle counts
- `gain_lsb_per_ut`: Sensitivity gain (LSB/$\mu$T)
- `scale_factor_nt_per_count`: Conversion factor (nT/count)
- `data_units`: `"raw counts (multiply by scale_factor_nt_per_count for nT)"`
- `start_time_iso`, `end_time_iso`, `duration_seconds`, and `sample_count`

---

## Build & Flash Commands

### Conda Environment Setup
```bash
conda env create -f environment.yml
conda activate rm3100
```

### Firmware Flashing
```bash
# Classic ESP32 Dev Module
pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0

# ESP32-C3 PCB
pio run -e esp32-c3-devkitm-1 -t upload --upload-port /dev/ttyACM0
```

### Launch Desktop Application
```bash
python desktop_app/main.py
```
