# Remote 3-Axis Magnetometer System — Documentation

Welcome to the documentation for the **Remote 3-Axis Magnetometer Acquisition System**.

---

## 📚 Core Manuals

1. [**1. Getting Started Guide**](1_GETTING_STARTED.md)
   - System architecture overview
   - Hardware pinouts & wiring (ESP32-C3, Heltec V4, nRF52840, RM3100, FLC100)
   - PlatformIO build environments & flashing commands
   - Provisioning wizard (`provision_node.py` & `provision_receiver.py`)

2. [**2. Firmware & Power Optimization Guide**](2_FIRMWARE_AND_POWER_GUIDE.md)
   - 10-sample binary batching architecture (`SensorBatchPacket`)
   - Interactive Serial CLI command reference for Sensor and Receiver nodes
   - Multi-protocol output modes (BLE Coded PHY, LoRa SX1262, ESP-NOW, Wi-Fi UDP)
   - Low-power techniques (Duty-cycled scanning, SX1262 sleep, OLED auto-sleep, clock gating)

3. [**3. Wireless & Field Testing Guide**](3_WIRELESS_AND_FIELD_TESTING.md)
   - Real-time phone monitoring using the **nRF Connect** app (Nordic UART Service)
   - Understanding signal strength: Sensor-to-Gateway RSSI vs. Gateway-to-Phone RSSI
   - Laptop Python client (`scripts/ble_gateway.py`)
   - Wireless range testing & link margin evaluation (`scripts/rssi_monitor.py`)

4. [**4. Central Server & Dashboards Guide**](4_CENTRAL_SERVER_AND_DASHBOARDS.md)
   - Self-contained FastAPI Central Server (`server.py`) and operations manager (`manage.sh`)
   - Docker Compose deployment & port mappings
   - REST API endpoints, WebSockets, CSV & HDF5 scientific export
   - Desktop PyQt5 application (`desktop_app/main.py`)
