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
   - Time-series plotting of 3-axis magnetic field ($B_x, B_y, B_z$) in Nanotesla ($\text{nT}$) and total vector magnitude ($|\mathbf{B}|$).
   - Built on `pyqtgraph` for high frame rates at up to 600 Hz sampling.

2. **Welch Power Spectral Density (PSD)**:
   - Compute real-time FFT frequency spectrum ($\text{nT}/\sqrt{\text{Hz}}$) with configurable FFT window sizes (256, 512, 1024, 2048, 4096 points) and window functions (Hann, Hamming, Blackman).
   - Real-time noise floor tracking for geomagnetic anomaly detection.

3. **HDF5 High-Performance Data Logger**:
   - Stream raw data directly to compressed HDF5 files (`.h5`) with Gzip chunk compression.
   - Preserves high-precision 64-bit microsecond timestamps (`timestamp_us`) and 32-bit float magnetic vector columns.

4. **Dynamic Hardware NVS Provisioning**:
   - Configure active device ID, custom rate codes, RM3100 cycle counts, and scale factor calibration values directly over USB serial.

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
