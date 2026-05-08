# Overview
This is an Arduino-style project for an ESP32 dev board supporting multiple 3-axis magnetometers (RM3100 via SPI and FLC-100 via Analog ADC).

The MCU features a Hardware Abstraction Layer (HAL) for sensors, provides a serial-based CLI for configuration, and streams 3-axis magnetic field data in CSV format. A companion desktop application is available in the `desktop_app` directory for real-time visualization and PSD analysis.

## Hardware Configuration
- **Microcontroller**: ESP32 (esp32dev)
- **Supported Sensors**:
    - **PNI RM3100**: High-resolution digital sensor (SPI).
    - **FLC-100**: High-sensitivity analog fluxgate (3-axis array via ADC).
- **Default RM3100 Pinout (SPI)**:
  - CS: GPIO 5 | DRDY: GPIO 4 | SCK: GPIO 18 | MISO: GPIO 19 | MOSI: GPIO 23
- **Typical FLC-100 Pinout (ADC)**:
  - X: GPIO 36 | Y: GPIO 39 | Z: GPIO 34 (Requires voltage dividers for 3.3V compatibility)

## CLI Commands
The serial interface operates at **115200 baud**. Commands are persisted to NVS:
- `HELP`: Show available commands.
- `STREAM ON/OFF`: Enable or disable continuous data streaming.
- `RATE <hex>`: Set the sampling rate code (Sensor specific).
- `CYCLE <int>`: Set the cycle count (RM3100 specific).
- `STATUS`: Display current streaming status, active sensor type, and health.

## Data Format
When streaming is enabled, data is output in CSV format:
`timestamp_us,x,y,z`
- `timestamp_us`: System uptime in **microseconds**.
- `x, y, z`: Magnetic field values (Raw counts for RM3100, nT for FLC-100).

## Software Environment
- **Conda Environment**: Use `environment.yml` to create the `rm3100` environment containing all Python and PlatformIO dependencies.
- **Desktop App**: Python-based tool with real-time plotting, configurable history buffers, and automated PSD analysis. Includes a `MOCK_SENSOR` mode for hardware-free testing.
- **Hardware Testing**: Automated validation suite located in `test/test_hardware.py`.

## Roadmap
- **Persistent Configuration**: (Implemented) Settings like streaming state and rate are saved in ESP32 NVS.
- **Host Software**: (Implemented) Real-time visualization and PSD analysis tool with circular buffering and auto-update.
- **Remote Sensor Mode**: Implement WiFi/Radio functionality for regular broadcasts and deep sleep.


