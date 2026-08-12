# Getting Started & System Overview (`docs/getting_started.md`)

This guide covers system prerequisites, Conda environment setup, python dependency installation, and a quickstart deployment procedure.

---

## 1. Prerequisites & Conda Environment Setup

### System Requirements
- **Host OS**: Linux (Ubuntu/Debian), macOS, or Windows
- **Python**: Python 3.10+
- **PlatformIO CLI**: Installed via `pip install platformio` or VS Code PlatformIO extension

### Installation Commands

```bash
# Clone the repository
git clone https://github.com/morgatron/remote_3_axis_magnetometer.git
cd remote_3_axis_magnetometer

# Create and activate Conda environment
conda env create -f environment.yml
conda activate rm3100
```

`environment.yml` includes all necessary dependencies (`fastapi`, `uvicorn`, `requests`, `pySide6`, `pyqtgraph`, `pandas`, `pyarrow`, `h5py`, `bleak`, `platformio`).

---

## 2. Supported Hardware Target Boards

The system supports multiple hardware configurations managed via [`include/board_config.h`](../include/board_config.h) and [`platformio.ini`](../platformio.ini):

1. **Heltec WiFi LoRa 32 V4 (ESP32-S3 + SX1262 LoRa)**:
   - Built-in sub-GHz SX1262 LoRa radio (+22 dBm).
   - Integrated 3.7V LiPo battery management circuit.
   - Vext power gating rail (GPIO 36).
2. **ESP32-C6 RISC-V PCB**:
   - Native Wi-Fi 6 (802.11ax) with Target Wake Time (TWT) for battery nodes.
   - Bluetooth 5.3 LE Long Range (Coded PHY S=8).
   - 802.15.4 radio (Thread / Zigbee support).
3. **ESP32-C3 RISC-V PCB**:
   - Ultra-compact single-core RISC-V board.
   - Native USB CDC serial.

---

## 3. End-to-End 5-Minute Quickstart

### Step 1: Start the Central Data Server
```bash
cd central_service
python server.py
```
Open `http://localhost:8000` in your web browser.

### Step 2: Flash & Connect the Gateway Receiver Node
Plug your receiver ESP32 (e.g. Heltec V4 or ESP32-C6) into USB-C:
```bash
pio run -e heltec_v4_receiver -t upload --upload-port /dev/ttyACM1
```

### Step 3: Run the Edge Relay Gateway Service
```bash
cd central_service
python gateway.py
```

### Step 4: Flash & Deploy Remote Sensor Node(s)
Plug your field sensor ESP32 into USB-C:
```bash
pio run -e esp32-c3-devkitm-1 -t upload --upload-port /dev/ttyACM0
```
By default, the sensor node boots in **Low-Power LE Coded PHY Batch Mode with Hardware ACKs** (`MODE BLE`, `BATCH 10`).

### Step 5: Configure BLE Batch Burst Rate via CLI
Connect to the Sensor Node CLI (`pio device monitor -p /dev/ttyACM0 -b 921600`) to customize batching:
```text
BATCH 10      # 10 samples per burst (10s sleep, maximum power savings)
BATCH 1       # 1 sample per burst (instantaneous 1 Hz streaming)
STATUS        # Confirm Device ID, sensor type, and BLE batch size
```

### Step 6: Verify Active Node Metrics
Open the Receiver Gateway CLI (`pio device monitor -p /dev/ttyACM1 -b 921600`) and type:
```text
NODES
```
You will see your active remote field sensor listed with MAC address, signal RSSI (dBm), battery voltage ($V_{bat}$), and total packet count.

---

## 4. Primary Field Operational Mode (BLE 5.0 Long Range + Hardware ACK)

The system's **primary operational mode** utilizes **Bluetooth 5.0 Extended Advertising (LE Coded PHY S=8)** with **Hardware ACKs (`AUX_SCAN_REQ`)**:

1. **Long-Range Penetration**: LE Coded PHY S=8 provides **+12 dB sensitivity gain** (~4x range multiplier over standard BLE) at **+15 dBm Max Power**.
2. **Duty Cycle Power Savings**: The sensor buffers 1 Hz telemetry in SRAM and powers on its BLE transmitter for only **200 ms every 10 seconds** (`BATCH 10`), reducing RF duty cycle to **< 0.5%** (~35 to 45 days battery life).
3. **Hardware ACKs & Disconnect Buffer**: The receiver automatically sends a 1-byte hardware `AUX_SCAN_REQ` upon packet reception. If the receiver goes out of range, the sensor retains up to **600 samples (10 Minutes of history)** in SRAM, automatically flushing the backlog once re-connected.
4. **Standalone Field Access Point (SoftAP) Provisioning**: For Wi-Fi field setups without a router, the Gateway Receiver SoftAP broadcasts `MAG_GATEWAY_XXXX` on `192.168.4.1`. Remote nodes can be provisioned using:
   ```bash
   python provision_node.py --port /dev/ttyACM0 --sensor MOCK --mode BOTH --ssid "MAG_GATEWAY_XXXX" --pass magnetometer123 --target 192.168.4.1
   ```
