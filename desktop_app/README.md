# 3-Axis Magnetometer Visualizer Desktop App

This PySide6 desktop application provides a modular, real-time visualization, PSD spectral analysis, and HDF5 scientific data acquisition platform for distributed 3-axis magnetometers.

## Prerequisites
- Python 3.8+ (included in the `rm3100` Conda environment)
- Microcontroller connected via USB Serial CDC or broadcasting over Wi-Fi UDP (Port 9876).

## Installation
```bash
cd desktop_app
pip install -r requirements.txt
```

## Usage
```bash
python main.py
```
1. Select the correct **Port** (e.g. `/dev/ttyACM0` or `WIFI_UDP (Port 9876)`).
   - Tip: Select `MOCK_SENSOR` to run in simulated mode without physical hardware.
2. Click **Connect**.
3. Use **Stream ON** / **Stream OFF** to control MCU streaming.
4. Switch to the **PSD Analysis** tab to view real-time Power Spectral Density.
5. Click **Provision Node...** to configure remote network and measurement parameters over Serial CLI.

## Modular Architecture

The desktop application is divided into self-contained, decoupled subsystems:

```
desktop_app/
├── main.py                  # Lightweight orchestrator MainWindow (< 300 lines)
├── core/
│   ├── data_buffer.py       # Thread-safe circular ring buffer with downsampling
│   └── hdf5_recorder.py     # Chunked Gzip-compressed HDF5 streaming recorder
├── widgets/
│   ├── time_series_plot.py  # PyQtGraph 3-axis plotting widget & real-time filters
│   ├── psd_plot.py          # Welch PSD spectral analysis widget
│   ├── stats_panel.py       # Real-time channel means display
│   ├── acquisition_sidebar.py # HDF5 recording & dynamic sensor controls
│   └── provision_dialog.py  # Modal dialog for ESP32 NVS provisioning
├── serial_worker.py         # Background serial reader thread
├── udp_worker.py            # Background UDP packet receiver thread
└── hdf5_loader.py           # Helper script for loading saved HDF5 files
```

## Loading Saved HDF5 Data

Saved acquisitions are stored in compressed scientific HDF5 (`.h5`):

```python
from hdf5_loader import load_hdf5_dataset

data, attrs = load_hdf5_dataset("acquisition.h5")
print(f"Loaded {len(data['time_s'])} samples recorded over {attrs.get('duration_seconds', 0):.2f}s")
print(f"X mean: {data['x'].mean():.2f} nT, Y mean: {data['y'].mean():.2f} nT, Z mean: {data['z'].mean():.2f} nT")
```
