# Overview
This repository contains the firmware and desktop visualization tool for an ESP32-based 3-axis magnetometer data acquisition system. It supports multiple high-precision magnetic sensor configurations:
1. **FLC-100 Analog Fluxgate Array**: Interfaced via an external **TI ADS131E08 24-bit 8-channel SPI ADC** (no analog voltage dividers needed on ESP32 ADC pins).
2. **PNI RM3100**: High-resolution digital magnetometer (SPI interface).

The microcontroller features a Hardware Abstraction Layer (HAL) for sensor control, a serial/UDP CLI for remote configuration, and streams 5-column magnetic field data at up to **1 kSPS** (600 Hz max continuous rate for RM3100). A single universal firmware binary supports both sensor models via NVS dynamic provisioning. A companion PySide6 desktop application (`desktop_app`) provides real-time multi-channel plotting, automated Welch Power Spectral Density (PSD) analysis, and direct real-time streaming to Gzip-compressed **HDF5 (`.h5`)** scientific datasets with embedded calibration metadata.

---

## Hardware Configuration & Sensor Scaling

- **Microcontroller**: ESP32 / ESP32-C3
- **Serial Interface**: **921,600 baud**
- **Supported Sensors**:
  - **FLC-100 Array + TI ADS131E08 24-bit ADC (SPI)**: Direct differential analog inputs to ADS131E08 (VREF = 2.4V). Operates at **2.0 MHz SPI clock** (`CLKSEL = 1` internal oscillator mode). Internal lead-off comparators are powered down (`FAULT = 0x00`) to prevent status byte insertions.
  - **PNI RM3100 (SPI)**: High-resolution digital sensor. Hardware Revision ID register (`0x36`) MUST equal `0x22`.

### RM3100 Dynamic nT Sensitivity & Scaling Formula
The physical sensitivity gain of the RM3100 ASIC depends on the configured oscillation cycle count ($N_C$):
$$\text{Gain}(N_C) = (0.3671 \times N_C + 1.5)\ \text{LSB}/\mu\text{T}$$

The conversion scale factor from 24-bit raw counts to nanoteslas (**nT**) is:
$$\text{Scale Factor (nT per count)} = \frac{1000.0}{\text{Gain}(N_C)}$$

Empirical testing confirms that dividing raw 24-bit integer counts directly by $\text{Gain}(N_C)$ yields an invariant, calibrated Earth magnetic field reading of **$\approx 46,000\text{ nT}$ ($\approx 46\ \mu\text{T}$)** across all cycle count settings ($50$, $100$, $200$, $400$).

### RM3100 High Sample Rate Cycle Count Capping
Measurement duration across 3 axes requires $T_{\text{meas}} = 3 \times (N_C \cdot T_{\text{LR}} + T_{\text{settle}})$. To prevent measurement durations from exceeding sampling rate intervals and locking up the ASIC state machine, `RM3100::setContinuousMode` automatically caps cycle counts for high-speed sampling rates:
- **150 Hz (`0x94`)**: Cycle count capped at $N_C \le 100$
- **300 Hz (`0x93`)**: Cycle count capped at $N_C \le 50$
- **600 Hz (`0x92`)**: Cycle count capped at $N_C \le 30$

### ASIC Live Register Update Rule (CMM Pause)
Per Section 5.2 (Page 29) of the RM3100 manual, register writes to `CCX/CCY/CCZ` (addresses `0x04` to `0x09`) while Continuous Measurement Mode (`CMM`) is active are ignored by the ASIC. `RM3100::setCycleCount` pauses `CMM` (`CMM = 0x00`), burst-writes the 6 cycle count registers in a single SPI CS transaction, and cleanly restarts `CMM`, enabling live cycle count adjustments during streaming.

---

## 5-Column Data Format

When data streaming is enabled, samples are output continuously over Serial and/or WiFi UDP in 5-column CSV format:

`timestamp_us,x,y,z,status`

- **`timestamp_us`**: System uptime in **microseconds** (64-bit `uint64_t`).
- **`x, y, z`**: Magnetic field values in nT or raw 24-bit ADC counts.
- **`status`**: 24-bit hex SPI status header string (6 uppercase hex digits, e.g. `C00000`). Valid measurement frames report `status == "C00000"`.

---

## Interactive CLI Commands

The CLI operates over Serial (**921,600 baud**) and WiFi UDP port **9876**. Commands are persisted to NVS:

- `HELP`: Display available commands.
- `STATUS`: Display active sensor type, streaming state, rate code, and register status.
- `STREAM ON` / `STREAM OFF`: Enable or disable continuous data streaming.
- `SENSOR <FLC100|RM3100>`: Set active sensor hardware model dynamically (`FLC100` for FLC100-ADS131E08, `RM3100` for PNI RM3100). Saves to NVS Flash and reboots MCU.
- `RATE <hex>`: Set hardware sampling / ADC rate code (e.g. `RATE 06` for 1 kSPS, `RATE 95` for RM3100 75 Hz, `RATE 92` for 600 Hz).
- `DOWNSAMPLE <int>`: Set software decimation factor for FLC100-ADS131E08 (e.g. `1` for 1 kSPS, `10` for 100 Hz, `100` for 10 Hz).
- `CYCLE <int>`: Set oscillation cycle count for RM3100 (e.g. `CYCLE 200`).
- `GAIN <int>`: Set ADS131E08 PGA gain (1, 2, 4, 8).
- `VREF <float>`: Set ADC reference voltage (default `2.4` V).
- `TEST ON` / `TEST OFF`: Enable/disable 1 Hz internal calibration square wave on ADS131E08.
- `MODE <SERIAL|WIFI|BOTH>`: Direct data stream output to Serial (USB testing), WiFi UDP (Remote deployment), or both.
- `WIFI <ssid> <password>` / `WIFI OFF` / `WIFI STATUS`: Configure WiFi station mode.
- `TARGET <ip>`: Set destination IP for WiFi UDP packet streaming.

---

## Desktop Application & HDF5 Data Logging

The Python desktop application (`desktop_app/main.py`) provides live data acquisition, real-time plotting, data logging, and node deployment provisioning:

### Key Features
1. **Real-time Visualization**: Multi-channel time-series plotting (X, Y, Z), live channel means in **nT** or counts, interactive history buffer, live low-pass filtering, and automated Welch Power Spectral Density (PSD) analysis.
2. **Direct Real-time HDF5 Streaming (`.h5` / `.hdf5`)**:
   - Streamed directly into Gzip-compressed resizable HDF5 datasets (`time_s`, `x`, `y`, `z`, `status`).
   - `h5_file.flush()` executes periodically during streaming so that all flushed data remains 100% valid and readable on disk even during unexpected power interruptions.
3. **Comprehensive Embedded Metadata Attributes**:
   - Automatically writes root metadata attributes to `f.attrs`:
     - `sensor_type`: `"RM3100"` or `"FLC100-ADS131E08"`
     - `rate_code_hex` & `rate_code_dec`: Selected sample rate code
     - `cycle_count_spinbox` & `cycle_count_active`: Configured cycle counts
     - `gain_lsb_per_ut`: Sensitivity gain (LSB/$\mu$T)
     - `scale_factor_nt_per_count`: Conversion factor (nT/count)
     - `data_units`: `"raw counts (multiply by scale_factor_nt_per_count for nT)"`
     - `start_time_iso`, `end_time_iso`, `duration_seconds`, and `sample_count`
4. **Hardware Auto-Detection & Control Switching**:
   - Probes `RM3100` via Revision ID `0x22`. Upon connection in **Auto-Detect** mode, the GUI automatically identifies the hardware model, updates the toolbar dropdown to **PNI RM3100 (Digital SPI)**, populates rate options, and activates the Cycle Count sidebar controls.
5. **Node Provisioning & Deployment Setup Dialog ("Provision Node...")**:
   - Allows one-click setup of Sensor Hardware (`FLC100` vs `RM3100`), ESP32 operational mode (`SERIAL` USB testing vs `WIFI` remote burst vs `BOTH`), WiFi network credentials, and auto-detected Target Ingestion Server IP.
   - Saves all parameters directly to ESP32 NVS Flash (`Preferences.h`) so that nodes recover settings instantly on power loss.

---

## Low-Power Remote Operation Architecture & Roadmap

This section outlines the architecture for deploying the node as an ultra-low-power, battery-operated remote sensor unit using **inter-sample ESP32 Light Sleep, delta binary compression, and fast WiFi UDP burst transmission**.

### 1. Power Budget & Sleep State Breakdown

| Power State | CPU State | WiFi Modem | Current (3.3V) | Use Case |
| :--- | :--- | :--- | :--- | :--- |
| **Active TX** | 160 MHz | ON (Transmitting) | **180 - 240 mA** | Fast burst UDP packet transmission (< 300 ms) |
| **Modem-Sleep** | 160 MHz | OFF | **15 - 20 mA** | SPI register configuration and initialization |
| **Light-Sleep** | Suspended (RAM active) | OFF | **150 - 200 µA** | Inter-sample pauses between hardware interrupt triggers |
| **Deep-Sleep** | OFF (RTC active) | OFF | **5 µA** | Extended deep sleep between transmission cycles |

### 2. Sensor Power Strategy (Preserving Sensitivity & Noise Floor)

To preserve **maximum magnetic sensitivity, zero thermal drift, and avoid settling hysteresis**, the analog front-end remains **continuously powered**:

- **FLC-100 Sensors (3x)**: $2\text{ mA}$ per sensor = **$6.0\text{ mA}$ total** @ 5V.
- **ADS131E08 ADC**: $2\text{ mW}$ per channel = **$\sim 2.5\text{ mA}$ total** @ 3.3V.
- **Total Continuous Analog Front-End Draw**: **$\sim 8.5\text{ mA}$**.

Power optimization focuses on the **ESP32 microcontroller** and **WiFi radio**:
* **Inter-Sample Light Sleep**: ESP32 enters Light Sleep ($150\ \mu\text{A}$) between sample interrupts.
* **Fast WiFi Burst Upload (< 300 ms)**: WiFi radio is powered ON only during high-speed static-IP UDP burst uploads.

### 3. Battery Life Projections (3000 mAh 18650 Li-ion Cell)

With the analog front-end continuously powered ($8.5\text{ mA}$) to maintain ultra-high sensitivity:
* **Continuous 100 Hz Sampling + 1-min WiFi Burst**: **~344 Hours (~14.3 Days / 2 Weeks)**
* **Continuous 10 Hz Sampling + 5-min WiFi Burst**: **~350 Hours (~14.5 Days)**

For digital **RM3100** deployments (SPI power-down $< 20\ \mu\text{A}$):
* **Periodic 1 kSPS Burst (2 sec) Every 15 min**: **~220 Days (~7 Months)**
* **Periodic 1 kSPS Burst (2 sec) Every 1 Hour**: **~2.5 Years**

---

## Software & Build Environment

- **Conda Environment**: Use `environment.yml` to set up the `rm3100` environment containing Python, PySide6, pyqtgraph, h5py, pandas, scipy, and PlatformIO dependencies:
  ```bash
  conda env create -f environment.yml
  conda activate rm3100
  ```
- **Firmware Compilation & Flashing**:
  ```bash
  pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0
  ```
- **Run Desktop Application**:
  ```bash
  python desktop_app/main.py
  ```
