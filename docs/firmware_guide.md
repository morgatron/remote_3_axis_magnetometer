# Firmware Flashing & Hardware Guide (`docs/firmware_guide.md`)

This guide covers building, flashing, configuring, and operating both the **Field Sensor Node Firmware** (`src/main.cpp`) and the **Multi-Protocol Receiver Gateway Firmware** (`src/receiver/`).

---

## 1. PlatformIO Target Environments

All target builds are configured in [`platformio.ini`](../platformio.ini):

### Field Sensor Node Targets
```bash
# Heltec V4 Sensor Node (ESP32-S3 + SX1262 LoRa)
pio run -e heltec_v4_sensor -t upload --upload-port /dev/ttyACM0

# ESP32-C6 Sensor Node (Wi-Fi 6 / 802.15.4)
pio run -e esp32-c6-devkitc-1 -t upload --upload-port /dev/ttyACM0

# ESP32-C3 Sensor Node PCB
pio run -e esp32-c3-devkitm-1 -t upload --upload-port /dev/ttyACM0
```

### Multi-Protocol Receiver Gateway Targets
```bash
# Heltec V4 Receiver Gateway (ESP32-S3 + SX1262 LoRa)
pio run -e heltec_v4_receiver -t upload --upload-port /dev/ttyACM1

# ESP32-C6 Receiver Gateway (Wi-Fi 6 / BLE Coded PHY / ESP-NOW)
pio run -e esp32c6_receiver -t upload --upload-port /dev/ttyACM1

# ESP32-C3 Receiver Gateway PCB
pio run -e esp32c3_receiver -t upload --upload-port /dev/ttyACM1
```

---

## 2. Pinout Reference Table

| Target Hardware Board | PlatformIO Env (`Sensor` / `Receiver`) | Sensor SCK | Sensor MOSI | Sensor MISO | Sensor CS | Sensor DRDY | LoRa CS / DIO1 / RST / BUSY | Vext Rail |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **ESP32-C3 RISC-V PCB** | `esp32-c3-devkitm-1` / `esp32c3_receiver` | GPIO 6 | GPIO 7 | GPIO 2 | GPIO 10 | GPIO 3 | GPIO 5 / 4 / 14 / 15 | N/A |
| **ESP32-C6 Wi-Fi 6 PCB** | `esp32-c6-devkitc-1` / `esp32c6_receiver` | GPIO 6 | GPIO 7 | GPIO 2 | GPIO 10 | GPIO 3 | GPIO 5 / 4 / 14 / 15 | N/A |
| **Heltec V4 (ESP32-S3 + SX1262)** | `heltec_v4_sensor` / `heltec_v4_receiver` | GPIO 41 | GPIO 42 | GPIO 40 | GPIO 39 | GPIO 38 | GPIO 8 / 14 / 12 / 13 | GPIO 36 |
| **Legacy ESP32 Dev (WROOM)** | `esp32dev` / `esp32dev_receiver` [Deprecated] | GPIO 18 | GPIO 23 | GPIO 19 | GPIO 5 | GPIO 4 | GPIO 5 / 4 / 14 / 15 | N/A |

---

## 3. Interactive Serial CLI Reference

Connect via USB serial at **921,600 baud**:

```bash
pio device monitor -b 921600
```

### Receiver Gateway CLI Commands
| Command | Description | Example |
| :--- | :--- | :--- |
| `HELP` / `STATUS` | Query receiver status, protocol packet counts, and network info | `STATUS` |
| `NODES` | Print real-time table of active remote sensor nodes with RSSI & Vbat | `NODES` |
| `MODE <SERIAL\|WIFI\|BOTH>` | Select egress relay destination (USB Serial, WiFi Network, or Dual Egress) | `MODE BOTH` |
| `WIFI <ssid> <pass>` | Save egress router credentials and connect to WiFi network | `WIFI MySSID secret123` |
| `TARGET <ip> [port]` | Configure target Central Server IP and port for WiFi forwarding | `TARGET 192.168.1.50 9876` |
| `CHANNEL <1-13>` | Set ESP-NOW WiFi radio channel | `CHANNEL 1` |
| `SAVE` | Persist current configuration to Flash NVS memory | `SAVE` |
| `REBOOT` | Restart receiver MCU | `REBOOT` |

### Sensor Node CLI Commands
| Command | Description | Example |
| :--- | :--- | :--- |
| `HELP` / `STATUS` | Query Device ID, sensor model, rate code, and hardware status | `STATUS` |
| `STREAM ON` / `OFF` | Enable / disable continuous telemetry stream | `STREAM ON` |
| `ID <name>` | Configure custom Device ID / Node name (saves to NVS Flash) | `ID NODE_ALPHA` |
| `SENSOR <RM3100\|FLC100\|MOCK>` | Set active sensor driver (`MOCK` = Range testing with synthetic telemetry and status `0x80MOCK`) | `SENSOR MOCK` |
| `RATE <hex>` | Set rate code (`0x95` = 75 Hz, `0x94` = 150 Hz, `0x93` = 300 Hz, `0x92` = 600 Hz) | `RATE 95` |
| `CYCLE <int>` | Set RM3100 oscillation cycle count | `CYCLE 200` |
| `MODE <SERIAL\|WIFI\|BLE\|BOTH>` | Route stream to USB Serial, WiFi UDP, BLE Long Range, or both | `MODE BOTH` |

---

## 4. Standalone Field Access Point (SoftAP) Architecture

The system supports **routerless field operation** using the Gateway Receiver's built-in Access Point:

```
+------------------------------------+        Direct Wi-Fi UDP        +-----------------------------------+
|     Remote Field Sensor Node       |  ----------------------------> |      Gateway Receiver Node        |
|  SSID: MAG_GATEWAY_XXXX            |      (Port 9876 @ 1 Hz)        |  SoftAP IP:  192.168.4.1          |
|  Assigned IP: 192.168.4.2           |                                |  SSID:       MAG_GATEWAY_XXXX    |
|  Target IP:   192.168.4.1          |                                |  Mode:       WIFI_AP_STA         |
+------------------------------------+                                +-----------------------------------+
                                                                                        |
                                                                             USB Serial | (or External STA)
                                                                                        v
                                                                      +-----------------------------------+
                                                                      |  Edge Relay / Central Data Server |
                                                                      +-----------------------------------+
```

- **Routerless Field SoftAP**: On boot, the Gateway Receiver launches a 2.4GHz WPA2 Access Point (`MAG_GATEWAY_XXXX`, Password: `magnetometer123`) on `192.168.4.1` (Channel 1). Remote sensor nodes connect directly to the Gateway without requiring an external 3rd-party Wi-Fi router or cellular hotspot.
- **Hybrid AP+STA Mode**: The Gateway Receiver runs in dual `WIFI_AP_STA` mode. It serves as the local Access Point for field sensors while maintaining an optional STA link to an external router (if configured via `WIFI "SSID" Pass`) for central server egress.
- **Standalone CLI Control**:
  - `WIFI CLEAR` / `WIFI OFF` (on Gateway): Clears external router credentials and locks the Gateway into standalone field SoftAP mode.
  - `WIFI STATUS` (on Gateway): Prints active SoftAP SSID (`MAG_GATEWAY_XXXX`), SoftAP IP (`192.168.4.1`), and external STA connection status.

---

## 5. Supported Radio Protocols

1. **Sub-GHz LoRa (Semtech SX1262)**:
   - Range: 2–10+ km (sub-GHz 868 / 915 MHz).
   - Ideal for long-range field sensor monitoring.
2. **ESP-NOW**:
   - Fast connectionless MAC-layer protocol (transmit time <1 ms).
   - Minimal power consumption for battery nodes.
3. **Bluetooth 5.3 LE / Coded PHY**:
   - Long Range (Coded PHY S=8) advertisement beacons & Nordic UART Service (NUS).
4. **WiFi UDP (SoftAP / Router STA)**:
   - Direct UDP packet streaming on port 9876 over local Field SoftAP (`192.168.4.1`) or infrastructure Wi-Fi networks.
