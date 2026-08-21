# Remote 3-Axis Magnetometer Acquisition System — User Documentation

Welcome to the user documentation for the **Remote 3-Axis Magnetometer Acquisition System**.

This system is designed for high-resolution, battery-powered remote magnetic field monitoring. It includes universal field sensor firmware, multi-protocol receiver gateway firmware, an edge relay service, a PySide6 desktop plotting application, and a lightweight central time-series server with a real-time Web GUI.

---

## 📚 User Documentation Sitemap

1. [**Getting Started & System Overview**](getting_started.md)
   - System architecture diagram
   - Conda environment setup & installation
   - End-to-end 5-minute quickstart guide
2. [**Firmware Flashing & Hardware Guide**](firmware_guide.md)
   - Supported hardware targets (Heltec V4, ESP32-C6, ESP32-C3)
   - Flashing sensor nodes and receiver gateways using PlatformIO
   - Interactive Serial CLI command reference (`NODES`, `STATUS`, `MODE`, `WIFI`, `TARGET`)
   - Radio protocols: Sub-GHz LoRa (SX1262), ESP-NOW, BLE / Coded PHY, WiFi UDP
3. [**Central Server & Web GUI Guide**](central_server_guide.md)
   - Running `server.py` and `gateway.py`
   - Using the Web GUI (`http://localhost:8000`)
   - REST API v1 reference & WebSockets streaming
   - Data exports: CSV, Apache Parquet (`.parquet`), NumPy (`.npz`), JSON
4. [**Server Hardware & Deployment Guide (Raspberry Pi 4 / Laptop)**](SERVER_SETUP_GUIDE.md)
   - Automated 1-click installer (`install.sh`)
   - Laptop lid-close sleep prevention & Wi-Fi power-save disabling
   - Raspberry Pi 4 SD card wear protection (WAL mode, log2ram, USB boot)
   - Operations management with `manage.sh` (status, live logs, online backups)
   - Single-command Docker Compose deployment
5. [**Residential NAT & Remote Access Guide**](NAT_AND_REMOTE_ACCESS.md)
   - Interactive NAT configuration assistant (`remote_access.sh`)
   - Tailscale Mesh VPN (Zero-config WireGuard, CGNAT bypass)
   - Cloudflare Tunnels (Public access & custom domain with free SSL)
   - Router Port Forwarding & Dynamic DNS (DuckDNS)
   - CGNAT diagnostic tests
6. [**Secrets Management & System Security Guide**](SECRETS_AND_SECURITY.md)
   - Storing server `API_KEY` with restricted file permissions (`0600`)
   - Cloudflare Tunnel token security & systemd storage
   - ESP32 dynamic NVS provisioning & hardware flash encryption
   - Git hygiene and credential leak prevention
7. [**Desktop Application Guide**](desktop_app_guide.md)
   - Running the PySide6 Desktop Application (`desktop_app/main.py`)
   - Real-time time-series plotting & Welch PSD spectral analysis
   - Device dynamic NVS provisioning & Gzip HDF5 (`.h5`) logging
8. [**LoRa SX1262 Testing & Setup Guide**](lora_testing_setup.md)
   - AU915 Band configuration (SF7 / 125 kHz / +22 dBm)
   - 10-sample batched bursting (`BATCH 10`) with `latest_sample_age_ms` time-on-air compensation
   - Automated testing tool (`scripts/setup_lora_test.py`)
9. [**BLE Coded PHY Long-Range Testing Guide**](simple_ble_testing_setup.md)
   - Bluetooth 5.0 LE Coded PHY S=8 batching
   - Hardware `AUX_SCAN_REQ` confirmation ACKs
   - Offline disconnect ring buffer and catch-up flushing

---

## 🚀 System Architecture Overview

```
 +------------------------+      Wireless Radio       +--------------------------+     USB Serial / WiFi     +-------------------------+
 |   Field Sensor Node    |  -----------------------> |    ESP32 Receiver Node   |  -----------------------> |   Central Data Server   |
 | (Heltec V4 / ESP32-C6) | (LoRa / ESP-NOW / BLE / UDP)| (Heltec V4 / ESP32-C6) |    (gateway.py / HTTP)    | (http://localhost:8000) |
 +------------------------+                           +--------------------------+                           +-------------------------+
```

### Supported Sensor Hardware
1. **PNI RM3100**: High-resolution digital 3-axis SPI magnetometer (Hardware REVID `0x22`).
2. **FLC-100 Analog Fluxgate Array**: Differential analog fluxgates sampled via external **TI ADS131E08 24-bit 8-channel SPI ADC** (VREF = 2.4V).
