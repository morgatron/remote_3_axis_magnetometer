# RM3100 Visualizer Desktop App

This application provides a real-time visualization and spectral analysis tool for the RM3100 magnetometer project.

## Prerequisites
- Python 3.8+
- The MCU should be connected via USB and programmed with the `mcu_v0` firmware.

## Installation
1.  Navigate to the `desktop_app` directory:
    ```bash
    cd desktop_app
    ```
2.  Install dependencies:
    Create a new python environment (using conda or virtualenv etc)
    ```bash
    pip install -r requirements.txt
    ```

## Usage
1.  Run the application:
    ```bash
    python main.py
    ```
2.  Select the correct **Port** from the dropdown. 
    *   **Tip**: Select `MOCK_SENSOR` to run in simulator mode without hardware.
3.  Click **Connect**.
4.  Use **Stream ON** to start receiving data.
5.  Switch to the **PSD Analysis** tab and click **Update PSD Now** (or enable **Auto-Update PSD**) to view the frequency spectrum. In simulator mode, the X-axis should show a clear peak at 5Hz.

## Features
- **Real-time Plotting**: High-performance visualization of X, Y, and Z axes using `pyqtgraph`.
- **Spectral Analysis**: Power Spectral Density (PSD) calculation using Welch's method from `scipy.signal`.
- **Simulator Mode**: Built-in mock data generator for testing visualization and PSD without a physical sensor.
- **MCU Control**: Directly send CLI commands to the microcontroller.
