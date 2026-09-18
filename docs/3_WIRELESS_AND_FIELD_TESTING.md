# 3. Wireless & Field Testing Guide

This guide covers field range verification, signal strength monitoring (RSSI), and phone/laptop client setup using Bluetooth 5.0 LE Coded PHY and LoRa.

---

## 1. Monitoring Telemetry on a Phone (nRF Connect)

The receiver gateway broadcasts connectionless 1 Mbps BLE **Extended Advertising** auxiliary PDUs under Company ID `0xFFFF` (`b"MG"`).

### Step-by-Step Instructions:
1. Open **nRF Connect** (Android / iOS) and go to the **Scanner** tab.
2. In the scanner filter, look for advertisements or raw manufacturer data from **`MAG_GATEWAY`** (or Company ID `0xFFFF`).
3. Tap on the advertised packet to view details:
   * **Manufacturer Data:** Contains the 31-byte `GatewayAdvPacket` header (33 bytes including 2-byte company ID) followed by 12-byte compact $(X, Y, Z)$ sensor samples.
   * **Company ID:** `0xFFFF` (Test/Custom)
   * **Payload Format:** Unpacked automatically by `scripts/ble_monitor.py` or nRF Connect raw payload inspector.
4. Every 10 seconds, the receiver emits a 1-second burst containing up to 18 samples from the remote sensor node.

---

## 2. Reading Link Signal Strength (RSSI)

There are two distinct signal strength metrics:

### A. Sensor-to-Gateway Link (Long-Range BLE Coded PHY / LoRa)
* **Where to find it:** The **9th (last) number** in every CSV line (e.g. `-45` in the sample above).
* **Evaluation Scale:**
  * **$-30\text{ to }-60\text{ dBm}$:** Excellent signal (close range).
  * **$-70\text{ to }-85\text{ dBm}$:** Good outdoor field signal.
  * **$-90\text{ to }-105\text{ dBm}$:** Fringe coverage limit.

### B. Gateway-to-Phone Link (Standard 1M BLE)
* In nRF Connect, tap the **three vertical dots $\mathbf{\vdots}$** in the top right $\to$ **Read RSSI** (or **Show RSSI graph**).

---

## 3. Laptop Telemetry & Diagnostic Monitor (`ble_monitor.py`)

Run the Python BLE connectionless scanner on Linux, macOS, or Windows. It passively listens for 1 Mbps Extended Advertising auxiliary PDUs from the receiver gateway without establishing a GATT connection or requiring pairing, or connects to an ESP32 BLE-to-serial bridge:

```bash
# Live stream to terminal (passive scanner)
python3 scripts/ble_monitor.py

# Live stream via hardware ESP32-C3 USB serial bridge
python3 scripts/ble_monitor.py --serial /dev/ttyACM2

# Collect N samples and exit cleanly
python3 scripts/ble_monitor.py --max-samples 40

# Run for a specific duration in seconds
python3 scripts/ble_monitor.py --timeout 45

# Save stream to CSV file
python3 scripts/ble_monitor.py --csv garden_session.csv

# Forward directly to Central Server HTTP endpoint
python3 scripts/ble_monitor.py --forward-url http://localhost:8000/api/v1/telemetry
```

### Gateway Diagnostic & Rendezvous Event Decoding
The gateway broadcasts real-time diagnostic packets (`sample_count == 0`) which `ble_monitor.py` parses automatically:
* **`[GATEWAY HEARTBEAT]`**: Emitted every 60s when idle, reporting gateway battery voltage and active tracking state.
* **`[GATEWAY STATUS] SYNC_ACQUIRED`**: Emitted upon locking to a remote sensor, reporting discovery duration in milliseconds.
* **`[GATEWAY ALERT] WINDOW_MISSED`**: Emitted when an expected burst window times out, reporting consecutive miss count and on-time.
* **`[GATEWAY ALERT] LOST_SYNC_DISCOVERY`**: Emitted after 3 consecutive misses, indicating the receiver has transitioned to discovery mode.
* **`[GATEWAY NOTICE] PERIODIC_LOOKOUT`**: Emitted during the 10-minute periodic scan for new field nodes.

---

## 4. Range Testing Utility (`rssi_monitor.py`)

To log and graph RSSI versus distance during outdoor walk tests:

```bash
python3 scripts/rssi_monitor.py --port /dev/ttyACM0 --output range_test.csv
```
