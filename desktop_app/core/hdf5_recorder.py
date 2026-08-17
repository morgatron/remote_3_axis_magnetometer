import os
import time
from datetime import datetime
import numpy as np
import h5py

class Hdf5Recorder:
    """
    High-performance HDF5 streaming recorder with chunked Gzip compression and metadata tracking.
    """
    def __init__(self, filepath: str, attrs: dict = None):
        self.filepath = filepath
        self.attrs = attrs or {}
        self.h5_file = None
        self.is_recording = False
        self.recorded_samples = 0
        self.start_time_unix = 0.0
        self.buffer = []
        self._init_file()

    def _init_file(self):
        self.h5_file = h5py.File(self.filepath, "w")
        self.start_time_unix = time.time()
        self.start_time_iso = datetime.now().astimezone().isoformat()

        self.h5_file.attrs['start_time_iso'] = self.start_time_iso
        self.h5_file.attrs['start_time_unix'] = self.start_time_unix

        for k, v in self.attrs.items():
            if v is not None:
                self.h5_file.attrs[k] = v

        chunk_sz = 32768
        self.dset_time = self.h5_file.create_dataset(
            'time_s', shape=(0,), maxshape=(None,), dtype='f8',
            chunks=(chunk_sz,), shuffle=True, compression='gzip', compression_opts=4
        )
        self.dset_x = self.h5_file.create_dataset(
            'x', shape=(0,), maxshape=(None,), dtype='f4',
            chunks=(chunk_sz,), shuffle=True, compression='gzip', compression_opts=4
        )
        self.dset_y = self.h5_file.create_dataset(
            'y', shape=(0,), maxshape=(None,), dtype='f4',
            chunks=(chunk_sz,), shuffle=True, compression='gzip', compression_opts=4
        )
        self.dset_z = self.h5_file.create_dataset(
            'z', shape=(0,), maxshape=(None,), dtype='f4',
            chunks=(chunk_sz,), shuffle=True, compression='gzip', compression_opts=4
        )
        self.dset_status = self.h5_file.create_dataset(
            'status', shape=(0,), maxshape=(None,), dtype='u4',
            chunks=(chunk_sz,), shuffle=True, compression='gzip', compression_opts=4
        )
        self.is_recording = True

    def add_sample(self, ts_us: int, x: float, y: float, z: float, status: int = 0xC00000) -> int:
        """Appends a sample tuple to the batch buffer. Flushes every 20 samples."""
        if not self.is_recording or not self.h5_file:
            return self.recorded_samples

        ts_sec = float(ts_us) / 1000000.0
        self.buffer.append((ts_sec, x, y, z, status))
        self.recorded_samples += 1

        if len(self.buffer) >= 20:
            self.flush()

        return self.recorded_samples

    def flush(self):
        """Flushes buffered sample tuples into HDF5 chunked datasets."""
        if not self.h5_file or not self.buffer:
            return
        try:
            batch = np.array(self.buffer, dtype=object)
            n_new = len(batch)
            curr = self.dset_time.shape[0]
            new_sz = curr + n_new

            self.dset_time.resize((new_sz,))
            self.dset_x.resize((new_sz,))
            self.dset_y.resize((new_sz,))
            self.dset_z.resize((new_sz,))
            self.dset_status.resize((new_sz,))

            self.dset_time[curr:new_sz] = batch[:, 0].astype(np.float64)
            self.dset_x[curr:new_sz] = batch[:, 1].astype(np.float32)
            self.dset_y[curr:new_sz] = batch[:, 2].astype(np.float32)
            self.dset_z[curr:new_sz] = batch[:, 3].astype(np.float32)
            self.dset_status[curr:new_sz] = batch[:, 4].astype(np.uint32)

            self.buffer.clear()
        except Exception as e:
            print(f"[HDF5 Flush Error] {e}")

    def close(self) -> int:
        """Flushes remaining data, records closing metadata attributes, and closes the file."""
        if not self.is_recording:
            return self.recorded_samples

        self.flush()
        if self.h5_file:
            total_samples = self.dset_time.shape[0]
            end_iso = datetime.now().astimezone().isoformat()
            end_unix = time.time()
            duration = end_unix - self.start_time_unix

            self.h5_file.attrs['end_time_iso'] = end_iso
            self.h5_file.attrs['end_time_unix'] = end_unix
            self.h5_file.attrs['duration_seconds'] = duration
            self.h5_file.attrs['sample_count'] = total_samples

            self.h5_file.flush()
            self.h5_file.close()
            self.h5_file = None

        self.is_recording = False
        return self.recorded_samples
