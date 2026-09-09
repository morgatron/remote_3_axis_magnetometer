# 1. Getting Started Guide

This guide covers initial hardware setup, wiring, toolchain configuration, firmware building, and provisioning for the Remote 3-Axis Magnetometer project.

---

## 1. System Overview

The system consists of three primary layers:

```
+---------------------------+       Wireless Burst (10s)       +---------------------------+       1 Mbps BLE / USB        +---------------------------+
|    Remote Sensor Node     | -------------------------------> |  Outside Receiver Gateway | ----------------------------> |  Host Computer / Phone    |
| (ESP32-C3 / Heltec V4 /   |    BLE Coded PHY (S=8) or        | (ESP32-C3 / Heltec V4)    |    (nRF Connect / Bleak /     | (Web Dashboard / Grafana/ |
|  nRF52840 + Mag Sensor)   |    SX1262 LoRa (915 MHz)         | (Wi-Fi OFF, Dual-PHY)     |     Serial / UDP Central)     |  Desktop PyQt5 HDF5 App)  |
+---------------------------+                                  +---------------------------+                               +---------------------------+
```

1. **Remote Sensor Node**: Gathers magnetic field vectors ($B_x, B_y, B_z$), packs 10 samples into a 139-byte binary batch, and transmits a 1-second burst every 10 seconds before sleeping.
2. **Receiver Gateway**: Receives the bursts over long-range RF (BLE Coded PHY or LoRa) with Wi-Fi powered OFF to save power, and relays the telemetry over standard 1 Mbps BLE, USB Serial, or Wi-Fi.
3. **Host Ingest**: Any phone (via **nRF Connect**) or laptop (via `scripts/ble_gateway.py`) displays real-time vectors and logs to CSV or Central Server.

---

## 2. Hardware Pinouts & Wiring

### A. ESP32-C3 SuperMini (Sensor & Receiver)
* **SPI Bus (RM3100 / ADS131E08 ADC):**
  * `SCK`: GPIO 8
  * `MISO`: GPIO 9
  * `MOSI`: GPIO 10
  * `CS`: GPIO 7
  * `DRDY`: GPIO 6

### B. Heltec WiFi LoRa 32 V3 / V4 (ESP32-S3 + SX1262 LoRa + OLED)
* **Integrated OLED (SSD1306):** SDA=17, SCL=18, RST=21, Vext=36
* **Integrated SX1262 LoRa:** CS=8, DIO1=14, RST=12, BUSY=13, SCK=9, MISO=11, MOSI=10
* **Battery Sensing:** ADC=GPIO 1, Divider Ctrl=GPIO 37 (4.90× divider)
* **User Wake Button:** GPIO 0 (`INPUT_PULLUP`)

---

## 3. Toolchain & Dependencies

* **PlatformIO CLI / Core:**
  ```bash
  pip install platformio
  ```
* **Python Host Tools:**
  ```bash
  pip install -r central_service/requirements.txt
  pip install bleak
  ```

---

## 4. PlatformIO Environments

The project defines modular PlatformIO targets in [`platformio.ini`](file:///home/morgan/Gropbox/SMACT2026/remote_3_axis_magnetometer/platformio.ini):

| Target Environment | Target Hardware | Role | Build Filter |
| :--- | :--- | :--- | :--- |
| `esp32-c3-devkitm-1` | ESP32-C3 SuperMini | Sensor Node | `+<*> -<receiver/*>` |
| `esp32c3_receiver` | ESP32-C3 SuperMini | Receiver Gateway | `+<receiver/*>` |
| `heltec_v4_sensor` | Heltec V4 (ESP32-S3) | Sensor Node | `+<*> -<receiver/*>` (`-D HELTEC_V4`) |
| `heltec_v4_receiver` | Heltec V4 (ESP32-S3) | Receiver Gateway | `+<receiver/*>` (`-D HELTEC_V4`) |

### Building and Flashing:
```bash
# Flash ESP32-C3 as Sensor Node (e.g. on /dev/ttyACM1)
pio run -e esp32-c3-devkitm-1 -t upload --upload-port /dev/ttyACM1

# Flash ESP32-C3 as Receiver Gateway (e.g. on /dev/ttyACM0)
pio run -e esp32c3_receiver -t upload --upload-port /dev/ttyACM0
```

---

## 5. Node Provisioning

Use the interactive provisioning wizard to configure node settings:

```bash
# Interactive configuration wizard
python3 scripts/provision_node.py -i

# Or set via CLI arguments
python3 scripts/provision_node.py --port /dev/ttyACM1 --id MAG_NODE_01 --mode BLE --batch 10

# Provision Receiver Gateway
python3 scripts/provision_receiver.py --port /dev/ttyACM0 --mode BLE
```
