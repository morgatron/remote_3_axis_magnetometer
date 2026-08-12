# Simple BLE Coded PHY Low-Power Testing Guide (Mock Data Mode)

This guide provides simple, step-by-step instructions to flash, provision, and verify the **Low-Power Bluetooth 5.0 LE Coded PHY Batching Configuration** using synthetic **MOCK sensor mode** (no physical RM3100 or FLC100 sensor hardware required).

---

## Hardware Requirements & Port Assumptions

* **1x Field Sensor Node**: ESP32-C3 DevKit (connected to `/dev/ttyACM1`) or Heltec V4.
* **1x Gateway Receiver Node**: Heltec V4 ESP32-S3 (connected to `/dev/ttyACM0`) or ESP32-C6.
* **1x USB Micro-B or USB-C Cable per device**.

---

## Step 1: Flash Firmware

Open a terminal in the root repository directory and flash both nodes:

```bash
# 1. Flash Field Sensor Node Firmware (Port /dev/ttyACM1)
pio run -e esp32-c3-devkitm-1 -t upload --upload-port /dev/ttyACM1

# 2. Flash Gateway Receiver Node Firmware (Port /dev/ttyACM0)
pio run -e heltec_v4_receiver -t upload --upload-port /dev/ttyACM0
```

> [!NOTE]
> If using Heltec V4 for the sensor node instead of ESP32-C3, use `-e heltec_v4_sensor`.

---

## Step 2: Provision & Configure Sensor Node (MOCK Mode)

Configure the Sensor Node over USB Serial (`/dev/ttyACM1` at 921,600 baud) to run synthetic **MOCK mode** with 10-sample low-power BLE batching:

### Command Sequence (Send via Serial Terminal or Python):
```text
SENSOR MOCK
ID NODE_3A
BATCH 10
MODE BOTH
STREAM ON
SAVE
```

### Explanation of Commands:
* **`SENSOR MOCK`**: Switches from physical SPI hardware to synthetic 3-axis magnetic field generator (3-phase sine wave in Nanotesla).
* **`ID NODE_3A`**: Assigns canonical `NODE_*` device ID prefix.
* **`BATCH 10`**: Configures 10-sample 10-second low-power batch burst mode (radio stays 100% OFF for 9.7s out of every 10s).
* **`MODE BOTH`**: Enables both USB Serial output and Bluetooth 5.0 LE Coded PHY Extended Advertising.
* **`STREAM ON`**: Enables continuous 1 Hz downsampled telemetry acquisition.
* **`SAVE`**: Persists settings to NVS Flash memory so the node powers up in this mode automatically across reboots.

---

## Step 3: Provision & Configure Receiver Node

Configure the Receiver Node over USB Serial (`/dev/ttyACM0` at 921,600 baud):

### Command Sequence:
```text
MODE SERIAL
SAVE
```

### Explanation of Commands:
* **`MODE SERIAL`**: Routes received remote sensor telemetry out to USB Serial CDC (921,600 baud) for Python script / gateway forwarding.
* **`SAVE`**: Persists egress configuration to NVS Flash memory.

---

## Step 4: Verification Methods

### Option A: Run Automated Multi-Device Integration Test
Run the end-to-end multi-device test suite:

```bash
python3 test/test_integration_sensor_receiver.py --sensor-port /dev/ttyACM1 --rcvr-port /dev/ttyACM0
```

**Expected Output**:
```text
--- Test 01: 10-Sample Coded PHY Batch Reception ---
Total samples received over 22s: 10
Verified 9 consecutive 1.0s batch intervals inside burst
.
--- Test 02: Disconnect Ring Buffer Catch-up Flushing ---
Sensor switched to MODE SERIAL (simulating 22s offline disconnect)...
Switching sensor back to MODE BOTH to trigger catch-up flush...
Total catch-up samples received: 10
.
--- Test 03: Dynamic Batch Size Change (BATCH 5) ---
Total samples received under BATCH 5: 10
.
----------------------------------------------------------------------
Ran 3 tests in 87.547s

OK
```

---

### Option B: Monitor Live Telemetry Stream via Python

Run a live monitor script on the Receiver port (`/dev/ttyACM0`):

```bash
python3 -c "
import serial, time
s = serial.Serial('/dev/ttyACM0', 921600, timeout=1.0)
print('Monitoring Receiver for LE Coded PHY batch bursts...')
while True:
    line = s.readline().decode('utf-8', errors='ignore').strip()
    if line.startswith('NODE_'):
        print('Rx Telemetry:', line)
"
```

**Expected Telemetry Stream Output**:
```text
Rx Telemetry: NODE_3A,20935000,21623.00,-3162.00,43222.00,004D4F,0.0,3.30,-42
Rx Telemetry: NODE_3A,21935000,21420.00,-3250.00,43233.00,004D4F,0.0,3.30,-42
Rx Telemetry: NODE_3A,22935000,21180.00,-3310.00,43210.00,004D4F,0.0,3.30,-42
...
```

Notice that every 10 seconds, a burst of 10 samples arrives with timestamps spaced exactly 1.0 second apart (`1000000 µs`), and the Receiver OLED screen `Rx Pkts` counter increments by +10.

---

## Step 5: Test Disconnect Catch-Up & Sensor Reboot Recovery

### 1. Offline Disconnect Catch-Up Test:
1. Send `MODE SERIAL` to the sensor on `/dev/ttyACM1` (disables BLE advertising, simulating an out-of-range disconnect).
2. Wait 20 seconds while the sensor accumulates 20 samples in its SRAM ring buffer.
3. Send `MODE BOTH` to the sensor on `/dev/ttyACM1`.
4. **Observation**: The sensor immediately transmits consecutive 300ms bursts back-to-back, flushing all 20 buffered catch-up samples to the receiver in ~1.5 seconds!

### 2. Sensor Reboot Recovery Test:
1. Send `REBOOT` to the sensor on `/dev/ttyACM1`.
2. **Observation**: The sensor restarts and sends post-reboot bursts. The receiver automatically detects the timestamp reset (`start_ts_ms < last_sample_ts_ms`), clears its de-duplication table, and continues receiving valid telemetry without freezing or locking up.
