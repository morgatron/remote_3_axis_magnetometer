# Remote 3-Axis Magnetometer Acquisition System

A high-resolution, battery-powered remote magnetic field monitoring ecosystem supporting PNI RM3100 digital SPI magnetometers and FLC100 analog fluxgates (sampled via 24-bit TI ADS131E08 ADC).

Features universal field sensor firmware, multi-protocol receiver gateway firmware (**LoRa**, **ESP-NOW**, **BLE Coded PHY**, **WiFi UDP**), an edge relay service, a PySide6 desktop plotting app, and a central server with a real-time Web GUI dashboard.

---

## 📖 User Documentation

Complete user documentation is available in the [`docs/`](docs/) directory:

- [**Documentation Index (`docs/README.md`)**](docs/README.md)
- [**Getting Started & System Overview (`docs/getting_started.md`)**](docs/getting_started.md)
- [**Firmware Flashing & Hardware Guide (`docs/firmware_guide.md`)**](docs/firmware_guide.md)
- [**Central Server & Web GUI Guide (`docs/central_server_guide.md`)**](docs/central_server_guide.md)
- [**Desktop Application Guide (`docs/desktop_app_guide.md`)**](docs/desktop_app_guide.md)
- [**Minimal End-to-End Testing Setup (`TESTING_SETUP.md`)**](TESTING_SETUP.md)

---

## 🚀 Quickstart

```bash
# 1. Clone & activate Conda environment
git clone https://github.com/morgatron/remote_3_axis_magnetometer.git
cd remote_3_axis_magnetometer
conda env create -f environment.yml
conda activate rm3100

# 2. Start Central Data Server
cd central_service && python server.py
# Open http://localhost:8000

# 3. Flash Heltec V4 Gateway Node
pio run -e heltec_v4_receiver -t upload --upload-port /dev/ttyACM1

# 4. Flash Heltec V4 Field Sensor Node
pio run -e heltec_v4_sensor -t upload --upload-port /dev/ttyACM0
```

---

## 🛠 Supported Hardware

| Hardware Board | Microcontroller | Radio Transceivers | Sensor Interfaces |
| :--- | :--- | :--- | :--- |
| **Heltec WiFi LoRa 32 V4** | ESP32-S3 (Dual-core LX7 @ 240 MHz) | Sub-GHz SX1262 LoRa (+22 dBm), Wi-Fi, BLE 5.0 | PNI RM3100 / ADS131E08 ADC |
| **ESP32-C6 RISC-V PCB** | ESP32-C6 (RISC-V @ 160 MHz) | Wi-Fi 6 (802.11ax), BLE 5.3 Coded PHY, 802.15.4 | PNI RM3100 / ADS131E08 ADC |
| **ESP32-C3 RISC-V PCB** | ESP32-C3 (RISC-V @ 160 MHz) | Wi-Fi 4, BLE 5.0 | PNI RM3100 / ADS131E08 ADC |
