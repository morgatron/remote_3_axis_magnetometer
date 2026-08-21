# Desktop Application Guide (`docs/desktop_app_guide.md`)

This guide covers running and using the **PySide6 Desktop Application** (`desktop_app/main.py`) for high-speed local serial visualization, spectral analysis, and HDF5 logging.

---

## 1. Running the Application

Ensure your Conda environment is activated (`conda activate rm3100`):

```bash
python desktop_app/main.py
```

---

## 2. Key Features & Interface Overview

1. **High-Speed Real-Time Plotting**:
   - Time-series plotting of 3-axis magnetic field ($B_x, B_y, B_z$) in calibrated Nanotesla ($\text{nT}$) and total vector magnitude ($|\mathbf{B}|$).
   - Built on `pyqtgraph` for high frame rates at up to 1000 Hz sampling (e.g. 100 Hz FLC100 or 75 Hz RM3100).
   - Automatic unit scaling in Nanotesla ($\text{nT}$).

2. **Welch Power Spectral Density (PSD)**:
   - Compute real-time FFT frequency spectrum ($\text{nT}/\sqrt{\text{Hz}}$) with configurable FFT window sizes (64 to 1024 points).
   - Real-time noise floor tracking for geomagnetic anomaly and noise detection.

3. **HDF5 High-Performance Data Logger**:
   - Stream raw data directly to compressed HDF5 files (`.h5`) with Gzip chunk compression.
   - Preserves high-precision 64-bit microsecond timestamps (`timestamp_us`) and 32-bit float magnetic vector columns.

4. **Dynamic Hardware NVS Provisioning**:
   - Direct support for **PNI RM3100** (digital cycle counts 50–400) and **FLC100 + ADS131E08** (24-bit ADC software decimation 1x–100x and PGA gains 1x–8x).
   - Configure active device ID, downsampling, and output modes directly over USB serial.

> [!NOTE]
> **Connecting over USB Serial**: Ensure the target node is configured in `MODE SERIAL` or `MODE BOTH` (`MODE SERIAL; STREAM ON; SAVE`). If the node is in `MODE WIFI` or `MODE BLE`, it will not stream over USB.

---

## 3. Data Export & Post-Processing

HDF5 files saved by the Desktop Application can be loaded into Python for analysis:

```python
import h5py
import numpy as np

with h5py.File("magnetometer_run_001.h5", "r") as f:
    timestamps = f["timestamps"][:]
    bx = f["x_nT"][:]
    by = f["y_nT"][:]
    bz = f["z_nT"][:]

print(f"Loaded {len(timestamps)} samples. Bx mean: {np.mean(bx):.2f} nT")
```
