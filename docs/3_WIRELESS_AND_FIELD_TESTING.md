# 3. Wireless & Field Testing Guide

This guide covers field range verification, signal strength monitoring (RSSI), and phone/laptop client setup using Bluetooth 5.0 LE Coded PHY and LoRa.

---

## 1. Monitoring Telemetry on a Phone (nRF Connect)

The receiver gateway broadcasts connectionless 1 Mbps BLE **Extended Advertising** auxiliary PDUs under Company ID `0xFFFF` (`b"MG"`).

### Step-by-Step Instructions:
1. Open **nRF Connect** (Android / iOS) and go to the **Scanner** tab.
2. In the scanner filter, look for advertisements or raw manufacturer data from **`MAG_GATEWAY`** (or Company ID `0xFFFF`).
3. Tap on the advertised packet to view details:
   * **Manufacturer Data:** Contains the 27-byte `GatewayAdvPacket` header followed by 12-byte compact $(X, Y, Z)$ sensor samples.
   * **Company ID:** `0xFFFF` (Test/Custom)
   * **Payload Format:** Unpacked automatically by `scripts/ble_gateway.py` or nRF Connect raw payload inspector.
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

## 3. Laptop Gateway Client (`ble_gateway.py`)

Run the Python BLE connectionless scanner on Linux, macOS, or Windows. It passively listens for 1 Mbps Extended Advertising auxiliary PDUs from the receiver gateway without establishing a GATT connection or requiring pairing:

```bash
# Live stream to terminal (passive scanner)
python3 scripts/ble_gateway.py

# Collect N samples and exit cleanly
python3 scripts/ble_gateway.py --max-samples 40

# Save stream to CSV file
python3 scripts/ble_gateway.py --csv garden_session.csv

# Forward directly to Central Server HTTP endpoint
python3 scripts/ble_gateway.py --forward-url http://localhost:8000/api/v1/telemetry
```

---

## 4. Range Testing Utility (`rssi_monitor.py`)

To log and graph RSSI versus distance during outdoor walk tests:

```bash
python3 scripts/rssi_monitor.py --port /dev/ttyACM0 --output range_test.csv
```
