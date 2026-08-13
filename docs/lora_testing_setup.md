# Heltec V4 SX1262 Sub-GHz LoRa Testing & Setup Guide

This document provides step-by-step instructions for flashing, provisioning, and testing **Sub-GHz LoRa (SX1262)** telemetry transmission between two **Heltec WiFi LoRa 32 V4 (ESP32-S3)** target boards.

---

## Hardware Pinout Reference (Heltec V4 Board)

| Signal | ESP32-S3 GPIO | Function |
| :--- | :--- | :--- |
| **SX1262 CS** | **GPIO 8** | LoRa SPI Chip Select |
| **SX1262 DIO1** | **GPIO 14** | LoRa Interrupt / RX Done |
| **SX1262 Reset** | **GPIO 12** | Hardware Reset Line |
| **SX1262 Busy** | **GPIO 13** | Hardware Status / Busy Line |
| **SX1262 SCK** | **GPIO 9** | SPI Clock |
| **SX1262 MOSI** | **GPIO 10** | SPI Master Out Slave In |
| **SX1262 MISO** | **GPIO 11** | SPI Master In Slave Out |
| **Vext Power Rail** | **GPIO 36** | Active LOW Power Rail Control (Drives OLED & LoRa power) |

---

## LoRa RF Frequency Configuration (AU915 Band)

The driver is pre-configured to operate under **Australian RF Spectrum Regulations (AU915 Band)**:

* **Frequency**: `915.0 MHz` (AU915 Band 915.0–928.0 MHz)
* **Bandwidth**: `500.0 kHz` (Configured for high data-rate 1 kSPS telemetry streaming)
* **Spreading Factor**: `SF7` (Lowest latency & maximum data throughput)
* **Coding Rate**: `4/5`
* **Output Power**: `+22 dBm` (Maximum SX1262 TX power output)

---

## Method 1: Automated Flashing & Over-The-Air Testing (Recommended)

Run the single-command automated test tool [`scripts/setup_lora_test.py`](file:///home/morgan/Gropbox/SMACT2026/remote_3_axis_magnetometer/scripts/setup_lora_test.py):

```bash
# 1. Automated Flash, Provision, and 30-second Live Telemetry Test
python3 scripts/setup_lora_test.py --sensor-port /dev/ttyACM1 --rcvr-port /dev/ttyACM0

# 2. Skip Flashing (Run Provisioning & Live Test Only)
python3 scripts/setup_lora_test.py --sensor-port /dev/ttyACM1 --rcvr-port /dev/ttyACM0 --skip-flash
```

---

## Method 2: Manual Flashing & CLI Provisioning

### Step 1: Flash Firmware via PlatformIO

```bash
# 1. Flash Receiver Firmware (Heltec V4 connected to /dev/ttyACM0)
pio run -e heltec_v4_receiver -t upload --upload-port /dev/ttyACM0

# 2. Flash Sensor Firmware (Heltec V4 connected to /dev/ttyACM1)
pio run -e heltec_v4_sensor -t upload --upload-port /dev/ttyACM1
```

---

### Step 2: Provision the Field Sensor Node (`/dev/ttyACM1`)

Open a terminal or serial monitor (`picocom -b 921600 /dev/ttyACM1`) and enter the following CLI commands:

```text
SENSOR MOCK
ID NODE_LORA_01
BATCH 10
MODE BOTH
STREAM ON
SAVE
```

---

### Step 3: Provision the Gateway Receiver Node (`/dev/ttyACM0`)

Open a terminal or serial monitor (`picocom -b 921600 /dev/ttyACM0`) and enter:

```text
MODE SERIAL
SAVE
```

---

## Live Monitoring Output

When telemetry packets arrive over the air, the Gateway Receiver (`/dev/ttyACM0`) outputs standard 6-column CSV telemetry lines with RSSI:

```text
NODE_LORA_01,1442000,21623.00,-3162.00,43222.00,004D4F,0.0,3.30,-48
NODE_LORA_01,1443000,21625.00,-3161.00,43224.00,004D4F,0.0,3.30,-48
```

---

## Troubleshooting

1. **`[LORA ERROR] Failed to initialize SX1262 module (code: -2)`**:
   - Ensure the Heltec Vext power rail is turned ON. In firmware setup, `initBoardPower()` pulls GPIO 36 `LOW`.
2. **No Packets Received Over-The-Air**:
   - Ensure antennas are securely attached to both Heltec V4 boards. Running LoRa transmitters without antennas can damage the RF power amplifier.
   - Verify both nodes are configured to the same frequency (`915.0 MHz`) and bandwidth (`500.0 kHz`).
