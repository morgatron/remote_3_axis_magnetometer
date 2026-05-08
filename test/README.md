# Hardware Validation Tests

This directory contains a suite of automated tests to verify the MCU firmware and sensor functionality when a physical ESP32 is connected.

## Prerequisites
- Python environment with `pytest` and `pyserial` installed (use the project's `environment.yml`).
- ESP32 connected via USB and programmed with the `mcu_v0` firmware.

## Running the Tests
The test script automatically attempts to find your ESP32 on typical serial ports (ttyUSBx, ttyACMx, COMx).

To run the suite:
```bash
pytest test/test_hardware.py
```

## What is tested?
1.  **Connectivity**: Ensures the serial port can be opened.
2.  **CLI Responsiveness**: Verifies that the `HELP` command returns the menu.
3.  **Sensor Status**: Verifies that `STATUS` reports valid sensor information.
4.  **Streaming Control**: Verifies that `STREAM ON` and `STREAM OFF` correctly start and stop the data flow.
5.  **Data Integrity**: Validates that the streaming data is in the correct `timestamp,x,y,z` CSV format.
