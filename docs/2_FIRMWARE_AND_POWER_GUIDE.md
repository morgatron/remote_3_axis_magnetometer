# 2. Firmware & Power Optimization Guide

This guide details the firmware architecture, batch transmission structures, interactive serial CLI commands, and low-power techniques implemented in both the Sensor and Receiver nodes.

---

## 1. Binary Packet Architecture (`TelemetryPacket.h`)

To maximize wireless range and minimize radio active time, all telemetry is packed into compact, unpadded binary structs:

```cpp
struct __attribute__((packed)) CompactSample {
    float x_nT; // Magnetic field X in nanoteslas
    float y_nT; // Magnetic field Y in nanoteslas
    float z_nT; // Magnetic field Z in nanoteslas
};

struct __attribute__((packed)) SensorBatchPacket {
    char          device_id[8];         // Null-terminated identifier (e.g. "NODE_3A8" or "CREEK")
    uint32_t      latest_sample_age_ms; // Age in ms of newest sample at instant of TX
    uint16_t      sample_interval_ms;   // Time between samples in ms (1000 ms = 1 Hz)
    uint8_t       sample_count;         // Number of samples in batch (up to 18)
    uint16_t      status;               // Status word
    uint16_t      vbat_mv;              // Battery voltage in mV
    CompactSample samples[18];          // Dynamic catch-up buffer (up to 235 bytes)
};

struct __attribute__((packed)) GatewayAdvPacket {
    uint16_t      company_id;           // 0xFFFF (Test / Non-registered company identifier)
    uint8_t       magic[2];             // 0x4D, 0x47 ("MG" for Mag Gateway)
    uint8_t       packet_seq;           // Transmission sequence counter (0-255)
    char          node_id[8];           // Field node identifier (null-terminated)
    uint64_t      timestamp_us;         // Microsecond timestamp of newest sample
    uint16_t      sample_interval_ms;   // Sample interval in ms (1000 ms)
    uint8_t       sample_count;         // Number of samples in packet (0 to 18)
    uint16_t      status;               // Status word
    uint16_t      vbat_mv;              // Battery voltage in mV
    int16_t       temp_c_x100;          // Temperature in deg C * 100 (0x7FFF = invalid/unmeasured)
    int8_t        rssi;                 // Signal strength in dBm
    uint16_t      gw_vbat_mv;           // Gateway battery voltage in mV
    CompactSample samples[18];          // Up to 18 samples (45 to 249 bytes)
};
```
* **Payload Compliance:** Both structures fit safely inside the standard 251-byte Bluetooth 5.0 LE Extended Advertising auxiliary PDU limit.

---

## 2. Sensor Node CLI Commands

Connect to the sensor node over USB serial at **921600 baud** to access the CLI:

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `HELP` / `STATUS` | None | Print system uptime, sensor status, battery voltage, and current settings |
| `ID <name>` | String | Set node identifier (e.g. `ID CREEK`) |
| `MODE <BLE\|LORA\|ESPNOW\|WIFI\|BOTH>` | Enum | Set primary wireless output protocol |
| `BATCH <1-18>` | Integer | Set batching size (e.g. `BATCH 10` sends 1 burst every 10 seconds; up to 18 for catch-up) |
| `SENSOR <RM3100\|FLC100\|MOCK>` | Enum | Select active magnetometer hardware |
| `RATE <0x90-0x98>` | Hex | Set RM3100 continuous sampling rate (`0x95` = 75 Hz) |
| `STREAM <ON\|OFF>` | None | Enable or disable live streaming |
| `SAVE` | None | Persist configuration to Flash NVS |
| `REBOOT` | None | Software reboot MCU |

---

## 3. Receiver Gateway CLI Commands

Connect to the receiver gateway at **921600 baud**:

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `HELP` / `STATUS` | None | Display receiver statistics, packet counts, and active sensor table |
| `NODES` | None | Print table of all tracked remote sensor nodes, RSSI, and last heard times |
| `MODE <BLE\|SERIAL\|WIFI\|BOTH>` | Enum | Set egress forwarding target (`BLE` activates connectionless 1M Extended Adv, Wi-Fi radio OFF) |
| `DEBUG [ON\|OFF]` | None / Enum | Toggle BLE rendezvous and discovery debug logging |
| `WIFI <ssid> <pass>` | Strings | Set router credentials for Wi-Fi egress mode |
| `TARGET <ip> [port]` | String, Int | Set target Central Server IP & UDP port for Wi-Fi egress |
| `CHANNEL <1-13>` | Integer | Set ESP-NOW Wi-Fi channel |
| `SAVE` | None | Save configuration to Flash NVS |
| `REBOOT` | None | Software reboot MCU |

---

## 4. Power Optimization & Sleep Architecture

### A. Sensor Node Optimizations:
1. **Low-Duty-Cycle Coded PHY Bursting:** The BLE radio is powered ON for only **300 ms** every 10 seconds (`BATCH 10`), remaining **100% OFF for 9.7 seconds** (>97% radio sleep duty cycle).
2. **Dynamic 18-Sample Catch-Up:** If the gateway is temporarily offline or out of range, un-ACKed samples accumulate in a circular ring buffer (up to 600 samples / 10 minutes) and burst up to 18 samples per transmission until caught up.
3. **Hardware `AUX_SCAN_REQ` ACKs:** Uses hardware scan requests to clear the ring buffer tail with zero transmission overhead.
4. **LoRa SX1262 Deep Sleep:** In `MODE LORA`, the SX1262 is put into deep sleep ($<1\,\mu\text{A}$) using `loraStream.sleep()`.
5. **Synchronized Battery Divider:** The resistive divider for $V_{bat}$ sensing is sampled strictly during the 10s burst wakeup, eliminating parasitic divider current between bursts.
6. **OLED Auto-Sleep:** Powers down after 30 seconds of inactivity (~20 mA savings) and re-awakens on **GPIO 0 (USER button)** press.

### B. Receiver Gateway Optimizations:
1. **Slotted Rendezvous Sleep Protocol:**
   - The receiver automatically tracks remote sensor burst intervals and calculates precise sleep intervals.
   - During the ~9.5-second gap between bursts, the BLE radio is shut down (`pScan->stop()`), achieving a **~98.6% radio sleep duty cycle** (dropping current from ~85 mA to ~16 mA).
   - The receiver wakes up with a **400 ms lead time** prior to the predicted burst, captures the incoming burst in ~100–150 ms, and immediately returns to sleep.
2. **Connectionless 1Mbps Extended Advertising Egress:**
   - Transmits telemetry bursts connectionlessly via BLE 5.0 1M Extended Advertising auxiliary PDUs (`Company ID 0xFFFF`, magic `b"MG"`).
   - Eliminates connection handshakes, GATT pairing delays, supervision timeouts, and slave latency requirements.
3. **Wi-Fi Radio Power-Down:** In `MODE BLE`, the Wi-Fi subsystem is shut down (`WiFi.mode(WIFI_OFF)`), saving **$\sim 80\text{ mA}$**.
4. **Sleep State Independence from USB CDC:**
   - In field deployment (battery powered, headless on a window ledge), the USB-CDC peripheral is completely unused, eliminating any concern regarding host USB-CDC suspension.
   - For tethered workbench development, keeping the CPU at 80 MHz preserves active USB CDC serial monitoring while the radio sleeps at 98.6% duty cycle.
