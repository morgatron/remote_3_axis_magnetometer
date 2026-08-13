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

6. **Edge Arrival Timestamping & $\Delta t$ Relative Reconstruction**:
   - Field nodes stream raw microsecond uptimes (`timestamp_us`) without requiring battery-backed RTC chips or NTP client stacks.
   - Upon packet arrival at the edge gateway (`gateway.py`), telemetry is anchored to the gateway's NTP-synchronized system UTC clock.
   - For multi-sample batches (e.g. after network drops or Store-and-Forward queue flushes), `stream_parser.parse_telemetry_batch` uses relative microsecond $\Delta t$ back-calculation to reconstruct exact 1.000-second historical sample spacing without clock drift.

7. **Modular Telemetry Ring Buffer (`include/TelemetryRingBuffer.h`)**:
   - Encapsulates 10-minute (600-sample) disconnect backlog storage into a thread-safe, self-contained C++ class.
   - Automatically handles FIFO eviction during prolonged outages, batch packaging for Coded PHY BLE Extended Advertising bursts, and hardware `AUX_SCAN_REQ` ACK confirmation flushes.
   - Standardized across all target boards on **NimBLE-Arduino 2.2.0** for BLE 5.0 Long Range Coded PHY support.

---

---

## Hardware Management Architecture (`include/board_config.h` & `platformio.ini`)

All hardware target boards are managed through a **centralized, single-source-of-truth configuration design**:

1. **Centralized Board Header (`include/board_config.h`)**:
   - Maps MCU preprocessor macros to physical board pinouts (Magnetometer SPI, LoRa SPI, status LEDs, and power rails).
   - Provides `initBoardPower()` to control hardware power gating rails (e.g. driving Heltec Vext GPIO 36 `LOW` during setup).
   - Abstracts hardware differences away from firmware business logic (`main.cpp` and `receiver_main.cpp`).

2. **DRY PlatformIO Configuration (`platformio.ini`)**:
   - Root `[env]` base section defines common parameters (`framework = arduino`, `monitor_speed = 921600`, upload flags, and native USB CDC defines).
   - Target environments inherit from `[env]` and select their role via `build_src_filter`:
     - **Field Sensor Node**: `build_src_filter = +<*> -<receiver/*>`
     - **Receiver & Gateway Node**: `build_src_filter = +<receiver/*>`

### Supported Hardware Pinout Reference Table

| Target Hardware Board | PlatformIO Env (`Sensor` / `Receiver`) | Sensor SCK | Sensor MOSI | Sensor MISO | Sensor CS | Sensor DRDY | LoRa CS / DIO1 / RST / BUSY | Vext Rail |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **ESP32-C3 RISC-V PCB** | `esp32-c3-devkitm-1` / `esp32c3_receiver` | GPIO 6 | GPIO 7 | GPIO 2 | GPIO 10 | GPIO 3 | GPIO 5 / 4 / 14 / 15 | N/A |
| **ESP32-C6 Wi-Fi 6 PCB** | `esp32-c6-devkitc-1` / `esp32c6_receiver` | GPIO 6 | GPIO 7 | GPIO 2 | GPIO 10 | GPIO 3 | GPIO 5 / 4 / 14 / 15 | N/A |
| **Heltec V4 (ESP32-S3 + SX1262)** | `heltec_v4_sensor` / `heltec_v4_receiver` | GPIO 41 | GPIO 42 | GPIO 40 | GPIO 39 | GPIO 38 | GPIO 8 / 14 / 12 / 13 | GPIO 36 |
| **Legacy ESP32 Dev (WROOM)** | `esp32dev` / `esp32dev_receiver` [Deprecated] | GPIO 18 | GPIO 23 | GPIO 19 | GPIO 5 | GPIO 4 | GPIO 5 / 4 / 14 / 15 | N/A |

### How to Add a New Hardware Board
To support a new board (e.g. ESP32-S3 DevKit or Raspberry Pi Pico 2 W):
1. Add a `#elif defined(YOUR_BOARD)` block in [`include/board_config.h`](file:///home/morgan/Gropbox/SMACT2026/remote_3_axis_magnetometer/include/board_config.h) defining SPI CS, SCK, MOSI, MISO, DRDY, and LoRa pins.
2. Add sensor and receiver target environment blocks in [`platformio.ini`](file:///home/morgan/Gropbox/SMACT2026/remote_3_axis_magnetometer/platformio.ini) with `-D YOUR_BOARD`.

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
| `HELP` / `STATUS` | Display CLI help / Query Device ID, sensor model, rate code, BLE batch size, and status |
| `STREAM ON` / `OFF` | Enable / disable continuous telemetry stream |
| `ID <name>` | Configure custom Device ID / Node name (saves to NVS Flash) |
| `SENSOR <FLC100\|RM3100\|MOCK>` | Set active sensor model (`MOCK` = synthetic range test telemetry) |
| `RATE <hex>` | Set rate code (`0x95` = 75 Hz, `0x94` = 150 Hz, `0x93` = 300 Hz, `0x92` = 600 Hz) |
| `CYCLE <int>` | Set RM3100 oscillation cycle count (e.g. `CYCLE 200`) |
| `BATCH <1-10>` | Set BLE burst size (`1` = instant 1 Hz, `10` = 10s low-power mode; saved to NVS) |
| `MODE <SERIAL\|WIFI\|BLE\|BOTH>` | Route stream to USB Serial, WiFi UDP, BLE Long Range, or both |

---

## Low-Power LE Coded PHY Batching & Hardware ACK Architecture (Primary Mode)

> [!IMPORTANT]
> **Canonical Primary Protocol Constraint**: The **Low-Power Bluetooth 5.0 LE Coded PHY Batching Protocol with Hardware ACKs and Disconnect Ring Buffering** is the canonical primary operational mode for field sensor nodes. Future edits MUST preserve this low-power ACK architecture.

1. **BLE 5.0 LE Coded PHY (Long Range S=8)**:
   - Extended Advertising using **Coded PHY S=8 (125 kbps)** provides **+12 dB sensitivity gain** (~4x range multiplier over standard BLE) at **+15 dBm Max Power**.

2. **Configurable Batch Bursting (`BATCH <1-10>`)**:
   - The BLE radio remains **100% OFF for 9.7 seconds** out of every 10 seconds (`BATCH 10`).
   - At burst time, `pAdvertising->start(0, 30)` powers on the radio for **300 ms** (30 x 10ms units) to broadcast a 139-byte Extended Advertising payload (`SensorBatchPacket`) containing up to 10 compact 3-axis samples over Coded PHY S=8.
   - **Transmission-Complete Auto-Stop**: When the 300ms burst completes, the `onStopped` hardware callback fires, setting `g_lastBatchAcked = true` and powering **OFF** the radio until the next 10-second window.
   - **RF Duty Cycle**: Reduces active RF transmission duty cycle to **< 0.5%**, extending battery life to **35–45 days** on a 1000 mAh LiPo.

3. **Hardware `AUX_SCAN_REQ` ACKs & 10-Minute Disconnect Ring Buffer**:
   - **Zero-Power Hardware ACKs**: Uses native Bluetooth 5.0 `AUX_SCAN_REQ` hardware scan request frames sent by the gateway upon receiving a burst (`onScanRequest` and `onStopped` callbacks).
   - **10-Minute Offline Buffer**: If the gateway goes out of range or offline, the sensor node retains up to **600 samples (10 Minutes of data)** in SRAM (~7.2 KB).
   - **Catch-up Flushing**: Once the gateway is back in range, the sensor node receives hardware ACKs and rapidly flushes the backlog in consecutive 300ms bursts until `unackedCount` returns to 0.
   - **Sensor Reboot Recovery**: Upon sensor reset, uptime timestamps restart near 0 ms. The Receiver `NodeTracker` detects `start_ts_ms < node.last_sample_ts_ms`, resetting `last_batch_start_ts_ms = 0xFFFFFFFF` to accept post-reset bursts continuously without stale timestamp deadlocks.
   - **Canonical Node Prefix Filtering**: Rejects non-sensor BLE devices by enforcing `NODE_*`, `MOCK_*`, and `MAG_*` device ID prefixes in `isValidBatchPacket` and `isValidSensorPacket`.
   - **Receiver OLED Telemetry Refresh**: Automatically refreshes `lastOledActivityMs = millis()` whenever valid telemetry packets arrive, keeping the OLED screen awake and updating while telemetry flows.

4. **Dynamic Frequency Scaling (DFS) Power Management**:
   - Configured with `min_freq = 20MHz`, `max_freq = 160MHz`, and `light_sleep_enable = false`.
   - **Why Light Sleep was disabled**: Automatic Light Sleep powers down the main 80 MHz APB bus clock and PLL. Re-locking the PLL incurs **1.5–3.0 ms of wake latency**, causing severe `DRDY` interrupt jitter and **sample loss** at 75 Hz sampling.
   - Keeping the APB clock powered ON (`light_sleep_enable = false`) guarantees **sub-microsecond (< 1 µs) interrupt latency with 0% sample loss**, while DFS throttles idle CPU power to 20 MHz (~3.5 mA system current).

5. **Low-Power Gateway Receiver Optimizations**:
   - **80 MHz CPU Scaling**: CPU frequency reduced from 240 MHz to **80 MHz** (`setCpuFrequencyMhz(80)`), saving ~28 mA.
   - **30-Second OLED Auto-Sleep**: Display controller sleeps after 30s of inactivity (~20 mA savings); wakes instantly via the **PRG / USER button (GPIO 0)** or Serial CLI input.
   - **Smart Wi-Fi Off**: Automatically turns off Wi-Fi radio when operating in USB Serial Egress mode (`MODE SERIAL`), saving ~80 mA.

---

## ESP32 Multi-Protocol Receiver & Data Relay Firmware (`src/receiver/`)

An ESP32 configured as a dedicated field receiver/relay ingests telemetry from battery-powered remote sensors across multiple wireless radio interfaces concurrently and forwards data to the host computer / central server.

### Key Receiver Features
1. **Multi-Protocol Wireless Ingestion**:
   - **ESP-NOW**: Ultra-low power, fast connectionless MAC-layer protocol (handles binary struct `SensorBinaryPacket` and string CSV payloads).
   - **BLE / BLE Coded PHY**: Bluetooth 5 Long Range advertisement and Nordic UART Service observer.
   - **WiFi UDP**: Listens on UDP port 9876 for field sensor broadcast packets.
2. **Dual Egress Relay**:
   - **USB Serial (CDC)**: Output formatted CSV stream at 921,600 baud directly to `gateway.py` or host PC.
   - **WiFi Egress**: Forwards batched payloads to Central Server HTTP endpoint or target UDP IP.
3. **Active Node Tracking**:
   - In-memory Node Table (`NODES` command) tracks device IDs, MAC addresses, RSSI signal strength, packet counts, last-seen timestamps, battery voltages, and ambient temperatures.

### Receiver CLI Commands
| Command | Description |
| :--- | :--- |
| `HELP` / `STATUS` | Display receiver status, protocol packet counts, and network info |
| `NODES` | Print real-time table of all active remote sensor nodes with RSSI & Vbat |
| `MODE <SERIAL\|WIFI\|BOTH>` | Select egress relay destination (USB Serial, WiFi Network, or Dual Egress) |
| `WIFI <ssid> <pass>` | Save egress router credentials and connect to WiFi |
| `TARGET <ip> [port]` | Configure target Central Server IP and port for WiFi forwarding |
| `CHANNEL <1-13>` | Set ESP-NOW WiFi radio channel |
| `SAVE` | Persist current configuration to NVS Flash memory |
| `REBOOT` | Restart receiver MCU |

---

## Running & Testing

> [!TIP]
> For step-by-step testing guides:
> - **Low-Power BLE Coded PHY Mock Testing**: See [`docs/simple_ble_testing_setup.md`](file:///home/morgan/Gropbox/SMACT2026/remote_3_axis_magnetometer/docs/simple_ble_testing_setup.md).
> - **Full System Hardware Pipeline Setup**: See [`TESTING_SETUP.md`](file:///home/morgan/Gropbox/SMACT2026/remote_3_axis_magnetometer/TESTING_SETUP.md).

### Conda Environment Setup
```bash
conda env create -f environment.yml
conda activate rm3100
```

### Firmware Build & Flash

```bash
# 1. Field Sensor Node Firmware (ESP32-C3 PCB)
pio run -e esp32-c3-devkitm-1 -t upload --upload-port /dev/ttyACM0

# 2. Field Sensor Node Firmware (ESP32-C6 Wi-Fi 6 / 802.15.4)
pio run -e esp32-c6-devkitc-1 -t upload --upload-port /dev/ttyACM0

# 3. Field Sensor Node Firmware (Heltec V4 ESP32-S3 + SX1262 LoRa)
pio run -e heltec_v4_sensor -t upload --upload-port /dev/ttyACM0

# 4. Multi-Protocol Receiver & Gateway (ESP32-C6 Wi-Fi 6)
pio run -e esp32c6_receiver -t upload --upload-port /dev/ttyACM1

# 5. Multi-Protocol Receiver & Gateway (Heltec V4 ESP32-S3 + SX1262 LoRa)
pio run -e heltec_v4_receiver -t upload --upload-port /dev/ttyACM1
```

### Central Data Server & Web GUI
```bash
cd central_service
python server.py
# Open http://localhost:8000
```

### Run Automated Test Suite
```bash
# 1. Binary Struct Serialization & Math Unit Tests (PC Host)
python3 -m unittest test/test_batch_serialization.py
python3 -m unittest test/test_receiver_parser.py
python3 test/test_scaling_math.py

# 2. Central Server API & Export Test
python3 central_service/test_server.py

# 3. Standalone Hardware Self-Tests
python3 test/test_sensor_standalone.py --port /dev/ttyACM1
python3 test/test_receiver_standalone.py --port /dev/ttyACM0

# 4. End-to-End Multi-Device Integration Test (Sensor + Receiver)
python3 test/test_integration_sensor_receiver.py --sensor-port /dev/ttyACM1 --rcvr-port /dev/ttyACM0
```
