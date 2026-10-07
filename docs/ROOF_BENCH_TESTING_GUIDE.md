# ROOF Heltec V4 Gateway & SPRINGBANK LoRa Benchtop Testing Guide

This guide provides step-by-step instructions for bench testing, provisioning, and verifying the **ROOF** gateway receiver (Heltec V4 ESP32-S3 + SX1262 LoRa) and **SPRINGBANK** sensor pair, operating with pure Sub-GHz SX1262 LoRa RF reception and direct USB Serial CDC telemetry egress (2.4 GHz Wi-Fi & BLE unpowered).

---

## 1. System Architecture

```
[SPRINGBANK Sensor Node]
  - Heltec V4 (ESP32-S3 + SX1262 LoRa)
  - FLC100 3-Axis Fluxgate Magnetometer (or synthetic MOCK for bench test)
  - 1 Hz Sampling, 10s Burst Interval (10 samples / burst @ +20 dBm LoRa)
  - 2.4 GHz Wi-Fi & BLE modems fully powered off to conserve battery
       │
       ▼ (LoRa 915 MHz AU915 / Sub-GHz RF Burst, +20 dBm, SF7, BW 125 kHz)
[ROOF Receiver Node]
  - Heltec V4 (ESP32-S3 + SX1262 LoRa)
  - SX1262 Radio in Continuous RX Mode (Sub-GHz LoRa ONLY)
  - 2.4 GHz Wi-Fi & BLE radios fully unpowered (WiFi.mode(WIFI_OFF), btStop())
  - Dynamic relative-to-host timestamping & OLED display
       │
       ├────────────────────────────────────────┬────────────────────────────────────────┐
       ▼ (Direct USB Serial CDC @ 921600 baud)   ▼ (OLED Display)                         ▼ (Optional Wi-Fi Egress)
  [Host PC / Edge Gateway / Logger]         Live Status & Counts                  [Central Data Server]
  - Direct CSV telemetry stream                                                    - HTTP POST batch endpoint
  - Clean CLI configuration interface                                              (Optional: 'MODE BOTH' or 'MODE WIFI')
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

### 1. Set Egress Mode to USB Serial (Default Profile)
For the pure LoRa-to-USB Serial setup (no Wi-Fi connection required, 2.4 GHz radio unpowered):
```text
MODE SERIAL
SAVE
```
*Note:* The receiver powers down its 2.4 GHz Wi-Fi and Bluetooth modems when in `SERIAL` mode, drawing ~20–25 mA.

### 2. Optional: Configure Wi-Fi & Central Server (If HTTP Egress Needed)
If you wish to route telemetry over Wi-Fi HTTP POST concurrently or exclusively:
- **Enterprise Wi-Fi:** `EAP ANU-Secure uXXXXXXX YourPassword`
- **Standard Wi-Fi:** `WIFI MyHotspot MyPassword123`
- **Destination Server:** `SERVER http://10.28.x.x:8000/api/v1/telemetry/batch`
- **Egress Mode:** `MODE BOTH` (Serial + HTTP) or `MODE WIFI` (HTTP only)
- **Save:** `SAVE`

---

## 5. Step 3: Verifying Connectivity on the Receiver

Once configured, verify device status using the diagnostic commands in the serial monitor:

### 1. Verify Status
```text
STATUS
```
Verify the output indicates:
```text
[STATUS] Receiver Status:
  Mode: SERIAL (USB Serial CDC Only, 2.4 GHz Wi-Fi/BLE OFF)
  LoRa: Frequency=915.0 MHz, SF=7, BW=125.0 kHz
  Egress Relayed Count: 0
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
2. Watch the **ROOF** receiver serial console. Every 10 seconds, you should observe:
   ```text
   [LORA RX] RssiPkt=126, SigRssi=126 -> RSSI=-63 dBm, SnrPkt=48 -> 12.00 dB
   SPRINGBA,0,21556.97,-3266.67,43115.65,008000,0.0,4.12,-63
   SPRINGBA,0,21527.97,-3300.84,43112.47,008000,0.0,4.12,-63
   SPRINGBA,0,21525.77,-3279.41,43119.60,008000,0.0,4.12,-63
   SPRINGBA,0,21530.07,-3309.20,43116.54,008000,0.0,4.12,-63
   SPRINGBA,0,21518.02,-3313.91,43106.42,008000,0.0,4.12,-63
   SPRINGBA,0,21478.99,-3294.02,43113.64,008000,0.0,4.12,-63
   SPRINGBA,0,21485.09,-3300.10,43107.64,008000,0.0,4.12,-63
   SPRINGBA,0,21458.43,-3315.46,43097.78,008000,0.0,4.12,-63
   SPRINGBA,0,21452.45,-3317.16,43095.95,008000,0.0,4.12,-63
   SPRINGBA,0,21454.93,-3338.29,43099.78,008000,0.0,4.12,-63
   ```
3. Observe the ROOF receiver OLED display:
   * Header: `ROOF GATEWAY`
   * Line 1: `WiFi: OFF (Serial Mode)`
   * Line 2: `Status: READY`
   * Line 3: `Last: SPRINGBA (-63dBm)`
   * Line 4: `Batches: 24 | Egress: SERIAL`

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
