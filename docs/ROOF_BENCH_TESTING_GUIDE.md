# ROOF Heltec V4 Gateway & SPRINGBANK LoRa Benchtop Testing Guide

This guide provides step-by-step instructions for bench testing, provisioning, and verifying the **ROOF** gateway receiver (Heltec V4 ESP32-S3 + SX1262 LoRa) and **SPRINGBANK** sensor pair, including WPA2-Enterprise (`ANU-Secure`) Wi-Fi, atomic NTP time synchronization, and HTTP batch forwarding to the Central Data Server.

---

## 1. System Architecture

```
[SPRINGBANK Sensor Node]
  - Heltec V4 (ESP32-S3 + SX1262 LoRa)
  - FLC100 3-Axis Fluxgate Magnetometer
  - 1 Hz Continuous Sampling, 10s Burst Interval (10 samples / burst)
       │
       ▼ (LoRa 915 MHz / Sub-GHz RF Burst)
[ROOF Gateway Node]
  - Heltec V4 (ESP32-S3 + SX1262 LoRa)
  - SX1262 Radio in Continuous RX Mode
  - Background NTP Client (pool.ntp.org)
  - Reconstructs sample ages into absolute millisecond UTC ISO 8601 timestamps
       │
       ├────────────────────────────────────────┬────────────────────────────────────────┐
       ▼ (HTTP POST /api/v1/telemetry/batch)    ▼ (Concurrent USB Serial CDC @ 921600)  ▼ (OLED Display)
[Central Data Server]                     [Host PC Serial Terminal]               Live Status & Counts
  - FastAPI + SQLite WAL Mode               - Real-time CSV logging & CLI
  - WebSocket Live Stream Broadcast
```

---

## 2. Prerequisites

1. **Hardware**:
   * 1x Heltec WiFi LoRa 32 V4 board for **ROOF** receiver gateway.
   * 1x Heltec WiFi LoRa 32 V4 board for **SPRINGBANK** sensor (or run `scripts/mock_roof_batch_sender.py` for server-only testing).
   * 915 MHz antennas connected to the SMA/IPEX ports on **both** Heltec boards before powering on (prevent RF amplifier damage).
   * USB-C data cables connecting boards to the host computer.
2. **Software Environment**:
   * Conda environment `rm3100` activated:
     ```bash
     conda activate rm3100
     ```
   * PlatformIO installed (`platformio --version` >= 6.1.0).

---

## 3. Step 1: Flashing the Firmware

### A. Flash the ROOF Receiver Gateway
Plug the receiver board into the host computer (identifies typically as `/dev/ttyACM0` or `/dev/ttyUSB0` on Linux):
```bash
pio run -e heltec_v4_receiver -t upload
```

### B. Flash the SPRINGBANK Sensor Node
Plug the sensor node into the host computer:
```bash
pio run -e heltec_v4_sensor -t upload
```

---

## 4. Step 2: Provisioning the ROOF Gateway via Serial CLI

Open a serial terminal connected to the **ROOF** receiver at **921600 baud**:
```bash
pio device monitor -b 921600
# Or using minicom / screen:
# minicom -D /dev/ttyACM0 -b 921600
```

Press `ENTER` or type `HELP` to view the CLI menu.

### 1. Check Device MAC Address
If device MAC registration is required by network administrators:
```text
MAC
```
*Output:*
```text
[WIFI] Station MAC: 34:85:18:XX:XX:XX
```

### 2. Configure Wi-Fi Credentials
Choose **one** of the following options based on your test environment:

#### Option A: ANU-Secure Enterprise Wi-Fi (WPA2-Enterprise PEAP)
```text
EAP ANU-Secure uXXXXXXX YourPassword
```
*Note:* The optional fourth parameter specifies the outer anonymous identity. If omitted, it defaults to the username.

#### Option B: Standard Wi-Fi / Test Hotspot (WPA2-PSK)
For home, lab, or phone hotspot testing:
```text
WIFI MyHotspot MyPassword123
```

### 3. Configure Central Server URL & API Key
Set the destination API endpoint and authorization token:
```text
SERVER http://10.28.x.x:8000/api/v1/telemetry/batch
APIKEY your_secret_api_key_here
```
*(If your server has no API key configured, `APIKEY` can be left blank).*

### 4. Enable Both Wi-Fi and Serial Egress
Set the egress mode to `BOTH` so telemetry is sent to the central server via HTTP POST while simultaneously outputting CSV over USB serial:
```text
MODE BOTH
```

### 5. Save Configuration to NVS Flash
Persist the network credentials, server URL, and mode across reboots:
```text
SAVE
```

---

## 5. Step 3: Verifying Connectivity on the Gateway

Once configured, verify connectivity using the diagnostic commands in the serial monitor:

### 1. Verify Wi-Fi and Status
```text
STATUS
```
Verify the output indicates:
```text
[STATUS] Receiver Status:
  Mode: BOTH (HTTP + Serial)
  WiFi: Connected (IP: 10.28.x.x, RSSI: -62 dBm)
  NTP:  Synchronized (UTC: 2026-10-04T08:30:15.120Z)
  Server: http://10.28.x.x:8000/api/v1/telemetry/batch
  LoRa: Frequency=915.0 MHz, SF=7, BW=125.0 kHz
```

### 2. Verify Atomic NTP Synchronization
```text
NTP
```
*Expected Output:*
```text
[NTP] Status: Synchronized
[NTP] Current UTC: 2026-10-04T08:30:15.120Z (Epoch: 1791102615)
```

### 3. Send an Immediate Synthetic Test Batch
To verify end-to-end HTTP posting without waiting for a sensor transmission:
```text
TESTPOST
```
*Expected Output:*
```text
[HTTP EGRESS] Sending synthetic test batch to http://10.28.x.x:8000/api/v1/telemetry/batch ...
[HTTP EGRESS] POST 1 samples -> HTTP 201 Created (42 ms)
```

---

## 6. Step 4: Verifying the SPRINGBANK $\rightarrow$ ROOF $\rightarrow$ Server Flow

1. Power up the **SPRINGBANK** sensor node (via battery or USB).
2. Watch the **ROOF** gateway serial console. Every 10 seconds, you should observe:
   ```text
   [LORA RX] Received batch from SPRINGBANK (10 samples, Age: 45 ms, RSSI: -72 dBm, SNR: 8.5 dB)
   [TIME] Anchoring samples: TX base age = 45 ms, NTP UTC = 2026-10-04T08:35:10.045Z
   [HTTP EGRESS] POST 10 samples -> HTTP 201 Created (38 ms)
   SPRINGBANK,2026-10-04T08:35:01.045Z,23415.20,-4120.80,48910.10,23.50,4.12,-72,0x000000
   SPRINGBANK,2026-10-04T08:35:02.045Z,23414.80,-4121.10,48911.20,23.50,4.12,-72,0x000000
   ...
   ```
3. Observe the ROOF gateway OLED display:
   * Header: `ROOF GATEWAY`
   * Line 1: `WiFi: OK (10.28.x.x)`
   * Line 2: `NTP: SYNCED`
   * Line 3: `Last: SPRINGBANK (-72dBm)`
   * Line 4: `Batches: 24 | Post: OK (201)`

---

## 7. Step 5: Testing Without Hardware (Software Mock Pipeline)

You can verify the Central Server and dashboard without any physical ESP32 boards connected using [`scripts/mock_roof_batch_sender.py`](file:///home/morgan/Gropbox/SMACT2026/remote_3_axis_magnetometer/scripts/mock_roof_batch_sender.py).

### Start the Local Central Server:
```bash
cd central_service
python3 server.py
# Server starts on http://localhost:8000
```

### Send a Single Synthetic Batch:
In a separate terminal:
```bash
python3 scripts/mock_roof_batch_sender.py --url http://localhost:8000 --once -v
```

### Run Continuous 10-Second Simulation:
```bash
python3 scripts/mock_roof_batch_sender.py --url http://localhost:8000 --batch-size 10 --interval 10.0
```

### Verify Persistence via HTTP API:
```bash
# Check registered nodes:
curl http://localhost:8000/api/v1/nodes

# Query latest 10 samples:
curl "http://localhost:8000/api/v1/data?node_id=SPRINGBANK&limit=10&format=json"

# Open the live web dashboard in a browser:
# Navigate to: http://localhost:8000
```

---

## 8. Troubleshooting

| Symptom | Probable Cause | Diagnostic Command / Fix |
| :--- | :--- | :--- |
| `WiFi: Connecting...` hangs indefinitely | Bad credentials, out of range, or 5 GHz-only SSID | Run `WIFI <ssid> <pass>` or `EAP <ssid> <user> <pass>` again. Note ESP32-S3 only supports 2.4 GHz channels 1–13. |
| `[NTP] Status: Unsynchronized` | UDP port 123 blocked by firewall or network | Check internet access. The gateway will continue functioning using relative timestamps until NTP synchronizes. |
| `[HTTP EGRESS] POST -> HTTP ERROR 401` | Server requires an API key, but gateway key is missing or wrong | Run `APIKEY <correct_key>` and `SAVE`. |
| `[HTTP EGRESS] POST -> HTTP ERROR 422` | Malformed payload | Ensure server is running latest version of `server.py` with `node_id` optional on batch telemetry points. |
| `[HTTP EGRESS] Connection Refused` | Server is down or IP address is wrong | Verify server IP via `curl http://<server_ip>:8000/health`. Update using `SERVER http://<server_ip>:8000/api/v1/telemetry/batch`. |
| LoRa packets not arriving | Mismatched frequency or missing antenna | Ensure both nodes use 915.0 MHz (`heltec_v4_sensor` and `heltec_v4_receiver`). Verify antennas are attached. |
