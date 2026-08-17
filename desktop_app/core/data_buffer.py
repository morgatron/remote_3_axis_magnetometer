import numpy as np

class DataBuffer:
    """
    Thread-safe circular ring buffer for real-time magnetometer time-series.
    """
    def __init__(self, max_samples: int = 2000):
        self.max_samples = max_samples
        self.time_buffer = np.zeros(self.max_samples, dtype=np.int64)
        self.x_buffer = np.zeros(self.max_samples, dtype=np.float32)
        self.y_buffer = np.zeros(self.max_samples, dtype=np.float32)
        self.z_buffer = np.zeros(self.max_samples, dtype=np.float32)
        self.status_buffer = np.zeros(self.max_samples, dtype=np.uint32)
        self.ptr = 0

    def add_sample(self, ts: int, x: float, y: float, z: float, status: int = 0xC00000):
        """Appends a new sample tuple to the circular ring buffer."""
        self.time_buffer[self.ptr] = ts
        self.x_buffer[self.ptr] = x
        self.y_buffer[self.ptr] = y
        self.z_buffer[self.ptr] = z
        self.status_buffer[self.ptr] = status
        self.ptr = (self.ptr + 1) % self.max_samples

    def resize(self, new_size: int) -> int:
        """
        Resizes the buffer while preserving existing historical samples.
        Returns the number of samples preserved.
        """
        if new_size == self.max_samples:
            return self.max_samples

        # Re-order existing ring data chronologically
        old_t = np.concatenate((self.time_buffer[self.ptr:], self.time_buffer[:self.ptr]))
        old_x = np.concatenate((self.x_buffer[self.ptr:], self.x_buffer[:self.ptr]))
        old_y = np.concatenate((self.y_buffer[self.ptr:], self.y_buffer[:self.ptr]))
        old_z = np.concatenate((self.z_buffer[self.ptr:], self.z_buffer[:self.ptr]))
        old_st = np.concatenate((self.status_buffer[self.ptr:], self.status_buffer[:self.ptr]))

        # Filter out empty zero timestamps
        valid = old_t > 0
        old_t, old_x, old_y, old_z, old_st = old_t[valid], old_x[valid], old_y[valid], old_z[valid], old_st[valid]

        self.max_samples = new_size
        self.time_buffer = np.zeros(self.max_samples, dtype=np.int64)
        self.x_buffer = np.zeros(self.max_samples, dtype=np.float32)
        self.y_buffer = np.zeros(self.max_samples, dtype=np.float32)
        self.z_buffer = np.zeros(self.max_samples, dtype=np.float32)
        self.status_buffer = np.zeros(self.max_samples, dtype=np.uint32)

        to_copy = min(len(old_t), self.max_samples)
        if to_copy > 0:
            self.time_buffer[:to_copy] = old_t[-to_copy:]
            self.x_buffer[:to_copy] = old_x[-to_copy:]
            self.y_buffer[:to_copy] = old_y[-to_copy:]
            self.z_buffer[:to_copy] = old_z[-to_copy:]
            self.status_buffer[:to_copy] = old_st[-to_copy:]
            self.ptr = to_copy % self.max_samples
        else:
            self.ptr = 0

        return to_copy

    def get_ordered_data(self):
        """
        Returns unwrapped chronological arrays (time_s, x, y, z, status).
        Only valid (non-zero timestamp) samples are returned.
        time_s is normalized in seconds relative to the first sample.
        """
        data_t = np.concatenate((self.time_buffer[self.ptr:], self.time_buffer[:self.ptr]))
        data_x = np.concatenate((self.x_buffer[self.ptr:], self.x_buffer[:self.ptr]))
        data_y = np.concatenate((self.y_buffer[self.ptr:], self.y_buffer[:self.ptr]))
        data_z = np.concatenate((self.z_buffer[self.ptr:], self.z_buffer[:self.ptr]))
        data_st = np.concatenate((self.status_buffer[self.ptr:], self.status_buffer[:self.ptr]))

        valid = data_t > 0
        if not np.any(valid):
            return np.array([]), np.array([]), np.array([]), np.array([]), np.array([])

        t_valid = data_t[valid]
        t_plot = (t_valid - t_valid[0]) / 1000000.0  # microseconds to seconds
        return t_plot, data_x[valid], data_y[valid], data_z[valid], data_st[valid]

    def get_raw_valid_data(self):
        """Returns unwrapped raw (t_us, x, y, z) arrays for spectral/PSD analysis."""
        data_t = np.concatenate((self.time_buffer[self.ptr:], self.time_buffer[:self.ptr]))
        data_x = np.concatenate((self.x_buffer[self.ptr:], self.x_buffer[:self.ptr]))
        data_y = np.concatenate((self.y_buffer[self.ptr:], self.y_buffer[:self.ptr]))
        data_z = np.concatenate((self.z_buffer[self.ptr:], self.z_buffer[:self.ptr]))

        valid = data_t > 0
        return data_t[valid], data_x[valid], data_y[valid], data_z[valid]
