# Overview
This repository contains the firmware and desktop visualization tool for an ESP32-based 3-axis magnetometer data acquisition system. It supports multiple high-precision magnetic sensor configurations:
1. **FLC-100 Analog Fluxgate Array**: Interfaced via an external **TI ADS131E08 24-bit 8-channel SPI ADC** (no analog voltage dividers needed on ESP32 ADC pins).
2. **PNI RM3100**: High-resolution digital magnetometer (SPI interface).

The microcontroller features a Hardware Abstraction Layer (HAL) for sensor control, a serial/UDP CLI for remote configuration, and streams 5-column magnetic field data at up to **1 kSPS**. A companion PySide6 desktop application (`desktop_app`) provides real-time multi-channel plotting, automated Welch Power Spectral Density (PSD) analysis, and direct real-time streaming to Gzip-compressed **HDF5 (`.h5`)** scientific datasets.

---

## Hardware Configuration

- **Microcontroller**: ESP32 / ESP32-C3
- **Serial Interface**: **921,600 baud**
- **Supported Sensors**:
  - **FLC-100 Array + TI ADS131E08 24-bit ADC (SPI)**: Direct differential analog inputs to ADS131E08 (VREF = 2.4V). Operates at **2.0 MHz SPI clock** (`CLKSEL = 1` internal oscillator mode). Internal lead-off comparators are powered down (`FAULT = 0x00`) to prevent status byte insertions.
  - **PNI RM3100 (SPI)**: High-resolution digital sensor.

### Default ESP32-C3 PCB Pinout
- **SCK**: GPIO 6
- **MOSI**: GPIO 7
- **MISO**: GPIO 2
- **DRDY**: GPIO 3 (Hardware interrupt pin)
- **CS**: GPIO 10
- **CLK_GEN**: GPIO 1 (Optional external clock output)

### Default ESP32 Dev Module Pinout
- **SCK**: GPIO 18 | **MOSI**: GPIO 23 | **MISO**: GPIO 19 | **CS**: GPIO 5 | **DRDY**: GPIO 4

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
- `RATE <hex>`: Set hardware sampling / ADC rate code (e.g. `RATE 06` for 1 kSPS, `RATE 96` for RM3100 ~37 Hz).
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
1. **Real-time Visualization**: Multi-channel time-series plotting (X, Y, Z), live channel means, interactive history buffer, live low-pass filtering, and automated Welch Power Spectral Density (PSD) analysis.
2. **Direct Real-time HDF5 Streaming (`.h5` / `.hdf5`)**:
   - Streamed directly into Gzip-compressed resizable HDF5 datasets (`time_s`, `x`, `y`, `z`, `status`).
   - `h5_file.flush()` executes periodically during streaming so that all flushed data remains 100% valid and readable on disk even during unexpected power interruptions.
3. **Embedded Metadata Attributes**:
   - Automatically writes `start_time_iso`, `start_time_unix`, `sensor_type`, `sample_rate_hz`, `vref_v`, and `sample_count` directly into HDF5 file header attributes (`f.attrs`).
4. **Dynamic Sensor Controls**:
   - Automatically detects connected sensor model and switches sidebar UI controls (Cycle counts for RM3100; Downsampling, Gain, and Test Signals for FLC100-ADS131E08).
5. **Node Provisioning & Deployment Setup Dialog ("Provision Node...")**:
   - Allows one-click setup of ESP32 operational mode (`SERIAL` USB testing vs `WIFI` remote burst vs `BOTH`), WiFi network credentials, and auto-detected Target Ingestion Server IP.
   - Saves all parameters directly to ESP32 NVS Flash (`Preferences.h`) so that nodes recover settings instantly on power loss.

### HDF5 Dataset Loader Module (`desktop_app/hdf5_loader.py`)

A helper module is included for opening and analyzing recorded datasets in Python scripts or Jupyter Notebooks:

```python
from desktop_app.hdf5_loader import load_magnetometer_h5

# Load dataset and metadata attributes
df, metadata = load_magnetometer_h5("acquisition.h5", filter_valid_only=True)

print("Start Time:", metadata["start_time_iso"])
print("Sensor:", metadata["sensor_type"])
print(df.head())
```

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

### 3. Fast WiFi Burst Upload Protocol (< 300 ms Active Window)
1. **Static IP Assignment**: Bypasses DHCP negotiation (`WiFi.config(ip, gateway, subnet)`).
2. **BSSID & Channel Caching**: Connects directly without scanning (`WiFi.begin(ssid, pass, channel, bssid)`).
3. **UDP Packet Bursting**: Transmits binary compressed packets without TCP handshake overhead.

### 4. Binary Delta Compression Pipeline (85% Size Reduction)
* **First-Difference Encoding**: Stores $\Delta X, \Delta Y, \Delta Z$ relative changes instead of 32-bit absolute values.
* **Size Reduction**: Reduces raw ASCII CSV size from **35 bytes/sample** down to **4–6 bytes/sample**, allowing 3,000 samples (~5 minutes of data) to fit within a single ~4 KB UDP burst payload.

### 5. Battery Life Projections (3000 mAh 18650 Li-ion Cell)

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
  pio run -e esp32-c3-devkitm-1 -t upload --upload-port /dev/ttyACM0
  ```
- **Run Desktop Application**:
  ```bash
  python desktop_app/main.py
  ```
