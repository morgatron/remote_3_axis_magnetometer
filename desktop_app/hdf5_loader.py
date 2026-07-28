"""
hdf5_loader.py - Magnetometer HDF5 Data Loader

Provides functions for loading, parsing, and filtering magnetometer HDF5 (.h5 / .hdf5) datasets
recorded by the 3-axis magnetometer desktop application.
"""

import os
import h5py
import numpy as np
import pandas as pd


def load_magnetometer_h5(filepath, filter_valid_only=False, return_dataframe=True):
    """
    Loads a magnetometer HDF5 dataset recorded by the desktop application.

    Parameters
    ----------
    filepath : str
        Path to the .h5 or .hdf5 file.
    filter_valid_only : bool, optional
        If True, filters out frames where status != 0xC00000 (corrupted frames). Default is False.
    return_dataframe : bool, optional
        If True, returns a pandas.DataFrame for data. If False, returns a dict of NumPy arrays. Default is True.

    Returns
    -------
    data : pandas.DataFrame or dict of np.ndarray
        Contains columns/keys: ['time_s', 'x', 'y', 'z', 'status', 'status_hex'].
    metadata : dict
        Contains metadata attributes stored in the HDF5 file header:
        - start_time_iso (str)
        - start_time_unix (float)
        - sensor_type (str)
        - sample_rate_hz (int)
        - sample_count (int)
        - vref_v (float)
    """
    if not os.path.exists(filepath):
        raise FileNotFoundError(f"HDF5 file not found: {filepath}")

    with h5py.File(filepath, "r") as f:
        # Extract metadata attributes
        metadata = dict(f.attrs)

        # Read datasets
        if "data" in f:
            raw_data = f["data"][:]
            time_s = raw_data["time_s"]
            x = raw_data["x"]
            y = raw_data["y"]
            z = raw_data["z"]
            status = raw_data["status"]
        else:
            time_s = f["time_s"][:]
            x = f["x"][:]
            y = f["y"][:]
            z = f["z"][:]
            status = f["status"][:]

    # Filter out corrupted frames if requested
    if filter_valid_only:
        valid_mask = (status == 0xC00000)
        time_s = time_s[valid_mask]
        x = x[valid_mask]
        y = y[valid_mask]
        z = z[valid_mask]
        status = status[valid_mask]

    if return_dataframe:
        df = pd.DataFrame({
            "time_s": time_s,
            "x": x,
            "y": y,
            "z": z,
            "status": status,
        })
        df["status_hex"] = df["status"].apply(lambda s: f"0x{s:06X}")
        return df, metadata
    else:
        status_hex = np.array([f"0x{s:06X}" for s in status])
        data_dict = {
            "time_s": time_s,
            "x": x,
            "y": y,
            "z": z,
            "status": status,
            "status_hex": status_hex
        }
        return data_dict, metadata


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1:
        target_file = sys.argv[1]
        print(f"Loading {target_file}...")
        df, meta = load_magnetometer_h5(target_file, filter_valid_only=True)
        print("\n--- Metadata Attributes ---")
        for k, v in meta.items():
            print(f"  {k}: {v}")
        print("\n--- First 5 Rows (Filtered) ---")
        print(df.head())
    else:
        print("Usage: python hdf5_loader.py <filename.h5>")
