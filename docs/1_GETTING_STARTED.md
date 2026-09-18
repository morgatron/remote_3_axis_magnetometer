# 1. Getting Started Guide

This guide covers initial hardware setup, wiring, toolchain configuration, firmware building, and provisioning for the Remote 3-Axis Magnetometer project.

---

## 1. System Overview

The system consists of three primary layers:

```
+---------------------------+       Wireless Burst (10s)       +---------------------------+       1 Mbps BLE / USB        +---------------------------+
|    Remote Sensor Node     | -------------------------------> |  Outside Receiver Gateway | ----------------------------> |  Host Computer / Phone    |
| (ESP32-C3 / XIAO ESP32-C6 |    BLE Coded PHY (S=8) or        | (ESP32-C3 / XIAO ESP32-C6 |    (nRF Connect / Bleak /     | (Web Dashboard / Grafana/ |
|  / Heltec V4 + Mag Sensor)|    SX1262 LoRa (915 MHz)         |  / Heltec V4, Wi-Fi OFF)   |     Serial / UDP Central)     |  Desktop PySide6 HDF5 App)|
+---------------------------+                                  +---------------------------+                               +---------------------------+
```

1. **Remote Sensor Node**: Gathers magnetic field vectors ($B_x, B_y, B_z$), packs 10 samples into a binary batch (with centi-degree temperature and battery voltage), and transmits over Coded PHY S=8 or LoRa before sleeping.
2. **Receiver Gateway**: Receives the bursts over long-range RF (BLE Coded PHY or LoRa) with Wi-Fi powered OFF to save power, and relays the telemetry over standard 1 Mbps BLE Extended Advertising, USB Serial, or Wi-Fi.
3. **Host Ingest**: Any phone (via **nRF Connect**) or laptop (via `scripts/ble_monitor.py`) displays real-time vectors and logs to CSV or Central Server.

---

## 2. Hardware Pinouts & Wiring

### A. ESP32-C3 SuperMini (Sensor & Receiver)
* **SPI Bus (RM3100 / ADS131E08 ADC):**
  * `SCK`: GPIO 6
  * `MOSI`: GPIO 7
  * `MISO`: GPIO 2
  * `DRDY`: GPIO 3
  * `CS`: GPIO 10
* **Status LED:** GPIO 8 (Active-LOW, Blue LED)
* **Battery Sensing:** GPIO 4 (ADC1_CH4, 2.0× resistive divider)

### B. Seeed Studio XIAO ESP32-C6 (Sensor & Receiver)
* **SPI Bus (RM3100 / ADS131E08 ADC):**
  * `SCK`: GPIO 1
  * `MOSI`: GPIO 2
  * `MISO`: GPIO 19
  * `DRDY`: GPIO 20
  * `CS`: GPIO 23
* **Status LED:** GPIO 15 (Active-LOW, Yellow LED)
* **Battery Sensing:** GPIO 0 (ADC1_CH0, 2.0× resistive divider)

### C. Heltec WiFi LoRa 32 V3 / V4 (ESP32-S3 + SX1262 LoRa + OLED)
* **SPI Bus (RM3100 / ADS131E08 ADC):**
  * `SCK`: GPIO 41
  * `MOSI`: GPIO 42
  * `MISO`: GPIO 40
  * `DRDY`: GPIO 38
  * `CS`: GPIO 39
* **Integrated OLED (SSD1306):** SDA=17, SCL=18, RST=21, Vext=36
* **Integrated SX1262 LoRa:** CS=8, DIO1=14, RST=12, BUSY=13, SCK=9, MISO=11, MOSI=10
* **Battery Sensing:** ADC=GPIO 1, Divider Ctrl=GPIO 37 (4.90× divider + P-MOSFET gate)
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
| `esp32-c6-devkitc-1` | Seeed XIAO ESP32-C6 | Sensor Node | `+<*> -<receiver/*>` |
| `esp32c6_receiver` | Seeed XIAO ESP32-C6 | Receiver Gateway | `+<receiver/*>` |
| `heltec_v4_sensor` | Heltec V4 (ESP32-S3) | Sensor Node | `+<*> -<receiver/*>` (`-D HELTEC_V4`) |
| `heltec_v4_receiver` | Heltec V4 (ESP32-S3) | Receiver Gateway | `+<receiver/*>` (`-D HELTEC_V4`) |

### Building and Flashing:
```bash
# Flash ESP32-C3 as Sensor Node (e.g. on /dev/ttyACM0)
pio run -e esp32-c3-devkitm-1 -t upload --upload-port /dev/ttyACM0

# Flash ESP32-C3 as Receiver Gateway (e.g. on /dev/ttyACM1)
pio run -e esp32c3_receiver -t upload --upload-port /dev/ttyACM1

# Flash Seeed XIAO ESP32-C6 as Sensor Node
pio run -e esp32-c6-devkitc-1 -t upload --upload-port /dev/ttyACM0
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
