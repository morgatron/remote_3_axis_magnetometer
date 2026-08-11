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
pio run -e heltec_v4_sensor -t upload --upload-port /dev/ttyACM0
```

### Step 5: Verify Active Node Metrics
Open the Receiver Gateway CLI (`pio device monitor -b 921600`) and type:
```text
NODES
```
You will see your active remote field sensor listed with MAC address, signal RSSI (dBm), battery voltage ($V_{bat}$), and total packet count.

---

## 4. Standalone Field Access Point (SoftAP) Provisioning

For outdoor / field deployments without an external Wi-Fi router:

1. **Gateway Receiver SoftAP**: Automatically broadcasts `MAG_GATEWAY_XXXX` (Password: `magnetometer123`) on `192.168.4.1`.
2. **Provisioning Command**:
   ```bash
   python provision_node.py --port /dev/ttyACM0 --sensor MOCK --mode BOTH --ssid "MAG_GATEWAY_XXXX" --pass magnetometer123 --target 192.168.4.1
   ```
3. **Automatic Reconnect**: The sensor node associates directly with the Gateway's SoftAP (`192.168.4.2`), streaming 1 Hz telemetry to `192.168.4.1:9876`.
