# Minimal End-to-End Testing & Deployment Guide (`TESTING_SETUP.md`)

This guide provides step-by-step instructions for setting up, deploying, and verifying a minimal end-to-end telemetry pipeline for the 3-Axis Magnetometer Acquisition System.

```
 +------------------------+      Wireless Radio       +--------------------------+     USB Serial / WiFi     +-------------------------+
 |   Field Sensor Node    |  -----------------------> |    ESP32 Receiver Node   |  -----------------------> |   Central Data Server   |
 | (Heltec V4 / ESP32-C6) | (LoRa / ESP-NOW / BLE / UDP)| (Heltec V4 / ESP32-C6) |    (gateway.py / HTTP)    | (http://localhost:8000) |
 +------------------------+                           +--------------------------+                           +-------------------------+
```

---

## 1. Start the Central Data Server

On your host computer or Raspberry Pi:

```bash
cd central_service
python server.py
```

- **Verification**: Open `http://localhost:8000` in your web browser. You will see the real-time Web GUI dashboard and WebSocket time-series plot.

---

## 2. Flash & Configure the Gateway Receiver Node

Flash an ESP32 board (e.g. Heltec V4 or ESP32-C6) with the dedicated receiver/relay firmware:

```bash
# Heltec V4 Receiver (ESP32-S3 + SX1262 LoRa)
pio run -e heltec_v4_receiver -t upload --upload-port /dev/ttyACM1

# Or ESP32-C6 Receiver (Wi-Fi 6 + BLE Coded PHY + ESP-NOW)
pio run -e esp32c6_receiver -t upload --upload-port /dev/ttyACM1

# Or ESP32-C3 Receiver PCB
pio run -e esp32c3_receiver -t upload --upload-port /dev/ttyACM1
```

### Egress Forwarding Options
- **USB Serial CDC (Default)**: Keep the gateway plugged into USB-C. It streams formatted CSV lines over USB Serial (921,600 baud) directly to the host PC running `gateway.py`.
- **WiFi Network Forwarding (Optional)**: Open Serial Monitor (`pio device monitor -b 921600`) and type:
  ```text
  WIFI <YourSSID> <YourPassword>
  TARGET <CentralServerIP> 9876
  MODE BOTH
  SAVE
  ```

---

## 3. Launch the Edge Gateway Relay Service

If your Gateway Receiver Node is connected to your host machine via USB:

```bash
cd central_service
python gateway.py
```

- `gateway.py` automatically reads USB Serial streams (`/dev/ttyACM1` / `/dev/ttyUSB1`) as well as WiFi UDP broadcasts on port 9876, and posts batched payloads to `http://localhost:8000/api/v1/telemetry/batch`.

---

## 4. Flash & Deploy Field Sensor Node(s)

Flash your remote battery-powered sensor board (connected to an RM3100 SPI magnetometer or FLC100/ADS131E08 ADC):

```bash
# Heltec V4 Field Sensor Node (ESP32-S3 + SX1262 LoRa)
pio run -e heltec_v4_sensor -t upload --upload-port /dev/ttyACM0

# Or ESP32-C6 Field Sensor Node (Wi-Fi 6 / 802.15.4)
pio run -e esp32-c6-devkitc-1 -t upload --upload-port /dev/ttyACM0

# Or ESP32-C3 Field Sensor Node PCB
pio run -e esp32-c3-devkitm-1 -t upload --upload-port /dev/ttyACM0
```

- **Auto-Detection**: The sensor node auto-probes the connected sensor hardware (RM3100 REVID `0x22` or ADS131E08 ADC) and streams calibrated physical Nanotesla ($\text{nT}$) data over sub-GHz LoRa, ESP-NOW, BLE, or WiFi UDP.

---

## 5. Verify Telemetry & Active Node Metrics

1. **Receiver CLI Node Table**: Open serial monitor on the Gateway Node (`pio device monitor -b 921600`) and type:
   ```text
   NODES
   ```
   You will see a real-time table of active remote nodes with MAC address, protocol source, RSSI signal strength (dBm), packet count, last seen timestamp, and battery voltage ($V_{bat}$).

2. **Central Web GUI**: Open `http://localhost:8000` to view live magnetic field vectors ($B_x, B_y, B_z$) and real-time PSD spectrum plots.

3. **Automated Test Suite (No Hardware Required)**:
   To test the software pipeline without physical hardware:
   ```bash
   # 1. Test Receiver Payload Parser & Microsecond Delta-t Reconstruction
   python test/test_receiver_parser.py

   # 2. Test Central Server API Endpoints & Data Export Formats
   python central_service/test_server.py
   ```
