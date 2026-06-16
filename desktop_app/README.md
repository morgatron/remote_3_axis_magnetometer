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
- **Binary Data Acquisition**: Stream large acquisitions directly to disk in a structured binary format. The app creates a temporary binary file to log data in real-time, then compiles it into a standard NumPy (`.npy`) format upon completion (or auto-stop timeout).

## Loading Saved Data
To load the compiled `.npy` acquisition data in a Python script:
```python
import numpy as np

# Load structured array
data = np.load("acquisition.npy")

# Access variables
timestamps = data['timestamp_us']
x = data['x']
y = data['y']
z = data['z']

print(f"Loaded {len(data)} samples.")
print(f"X mean: {np.mean(x):.2f}, Y mean: {np.mean(y):.2f}, Z mean: {np.mean(z):.2f}")
```
