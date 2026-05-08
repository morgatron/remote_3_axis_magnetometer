import sys
import json
import os
import numpy as np
from scipy import signal
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                             QHBoxLayout, QPushButton, QComboBox, QLabel, 
                             QTabWidget, QStatusBar, QLineEdit, QCheckBox)
from PySide6.QtCore import Slot, Qt, QTimer
import pyqtgraph as pg
import serial.tools.list_ports
from serial_worker import SerialWorker

CONFIG_FILE = "config.json"

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("RM3100 Magnetometer Visualizer")
        self.resize(1000, 700)

        # Data buffers
        self.max_samples = 2000
        self.time_buffer = np.zeros(self.max_samples)
        self.x_buffer = np.zeros(self.max_samples)
        self.y_buffer = np.zeros(self.max_samples)
        self.z_buffer = np.zeros(self.max_samples)
        self.ptr = 0  # Write pointer

        self.init_ui()
        self.load_config()
        self.serial_thread = None
        
        # Timer for Auto-PSD
        self.psd_timer = QTimer()
        self.psd_timer.timeout.connect(self.update_psd)
        self.psd_timer.start(1000) # Check every second

    def init_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)

        # Connection Panel
        conn_layout = QHBoxLayout()
        self.port_combo = QComboBox()
        self.refresh_ports()
        conn_layout.addWidget(QLabel("Port:"))
        conn_layout.addWidget(self.port_combo)

        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self.toggle_connection)
        conn_layout.addWidget(self.connect_btn)

        refresh_btn = QPushButton("Refresh")
        refresh_btn.clicked.connect(self.refresh_ports)
        conn_layout.addWidget(refresh_btn)
        
        conn_layout.addStretch()
        main_layout.addLayout(conn_layout)

        # Control Panel
        ctrl_layout = QHBoxLayout()
        self.stream_btn = QPushButton("Stream ON")
        self.stream_btn.clicked.connect(lambda: self.send_mcu_command("STREAM ON"))
        ctrl_layout.addWidget(self.stream_btn)

        self.stop_stream_btn = QPushButton("Stream OFF")
        self.stop_stream_btn.clicked.connect(lambda: self.send_mcu_command("STREAM OFF"))
        ctrl_layout.addWidget(self.stop_stream_btn)

        self.rate_input = QLineEdit("92")
        self.rate_input.setPlaceholderText("Rate (hex)")
        self.rate_input.setFixedWidth(60)
        ctrl_layout.addWidget(QLabel("Rate (hex):"))
        ctrl_layout.addWidget(self.rate_input)
        
        set_rate_btn = QPushButton("Set Rate")
        set_rate_btn.clicked.connect(self.set_mcu_rate)
        ctrl_layout.addWidget(set_rate_btn)

        ctrl_layout.addSpacing(20)
        ctrl_layout.addWidget(QLabel("FFT Window:"))
        self.nperseg_combo = QComboBox()
        for val in ["64", "128", "256", "512", "1024"]:
            self.nperseg_combo.addItem(val)
        self.nperseg_combo.setCurrentText("256")
        ctrl_layout.addWidget(self.nperseg_combo)

        ctrl_layout.addSpacing(20)
        ctrl_layout.addWidget(QLabel("History:"))
        self.history_combo = QComboBox()
        for val in ["500", "1000", "2000", "5000", "10000"]:
            self.history_combo.addItem(val)
        self.history_combo.setCurrentText("2000")
        self.history_combo.currentTextChanged.connect(self.update_buffer_size)
        ctrl_layout.addWidget(self.history_combo)

        self.auto_psd_cb = QCheckBox("Auto-Update PSD")
        self.auto_psd_cb.setChecked(True)
        ctrl_layout.addWidget(self.auto_psd_cb)

        main_layout.addLayout(ctrl_layout)

        # Tabs for Visualization
        self.tabs = QTabWidget()
        main_layout.addWidget(self.tabs)

        # Tab 1: Time Series
        self.time_plot_widget = pg.PlotWidget(title="Real-time Magnetometer Data")
        self.time_plot_widget.addLegend()
        self.time_plot_widget.setLabel('left', 'Magnetic Field (Counts/nT)')
        self.time_plot_widget.setLabel('bottom', 'Time (s)')
        self.time_plot_widget.showGrid(x=True, y=True)
        
        self.curve_x = self.time_plot_widget.plot(pen='r', name='X')
        self.curve_y = self.time_plot_widget.plot(pen='g', name='Y')
        self.curve_z = self.time_plot_widget.plot(pen='b', name='Z')
        
        self.tabs.addTab(self.time_plot_widget, "Time Series")

        # Tab 2: PSD
        self.psd_plot_widget = pg.PlotWidget(title="Power Spectral Density (Welch)")
        self.psd_plot_widget.addLegend()
        self.psd_plot_widget.setLabel('left', 'Power/Frequency (dB/Hz)')
        self.psd_plot_widget.setLabel('bottom', 'Frequency (Hz)')
        self.psd_plot_widget.setLogMode(x=False, y=True)
        self.psd_plot_widget.showGrid(x=True, y=True)

        self.psd_curve_x = self.psd_plot_widget.plot(pen='r', name='X')
        self.psd_curve_y = self.psd_plot_widget.plot(pen='g', name='Y')
        self.psd_curve_z = self.psd_plot_widget.plot(pen='b', name='Z')

        psd_tab_layout = QVBoxLayout()
        psd_tab_layout.addWidget(self.psd_plot_widget)
        calc_psd_btn = QPushButton("Update PSD Now")
        calc_psd_btn.clicked.connect(self.update_psd)
        psd_tab_layout.addWidget(calc_psd_btn)
        
        psd_tab_widget = QWidget()
        psd_tab_widget.setLayout(psd_tab_layout)
        self.tabs.addTab(psd_tab_widget, "PSD Analysis")

        # Status Bar
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)

    def refresh_ports(self):
        self.port_combo.clear()
        ports = serial.tools.list_ports.comports()
        for p in ports:
            # Filter for typical USB-serial devices (ttyUSBx, ttyACMx, or COMx on Windows)
            device = p.device
            if any(pattern in device for pattern in ["ttyUSB", "ttyACM", "COM"]):
                self.port_combo.addItem(device)
        
        # Always allow the simulator
        if self.port_combo.findText("MOCK_SENSOR") == -1:
            self.port_combo.addItem("MOCK_SENSOR")

    def update_buffer_size(self):
        try:
            new_size = int(self.history_combo.currentText())
            if new_size == self.max_samples:
                return
            
            # Re-order and preserve existing data
            old_t = np.concatenate((self.time_buffer[self.ptr:], self.time_buffer[:self.ptr]))
            old_x = np.concatenate((self.x_buffer[self.ptr:], self.x_buffer[:self.ptr]))
            old_y = np.concatenate((self.y_buffer[self.ptr:], self.y_buffer[:self.ptr]))
            old_z = np.concatenate((self.z_buffer[self.ptr:], self.z_buffer[:self.ptr]))
            
            # Keep only non-zero samples
            valid = old_t > 0
            old_t, old_x, old_y, old_z = old_t[valid], old_x[valid], old_y[valid], old_z[valid]
            
            # Resize
            self.max_samples = new_size
            self.time_buffer = np.zeros(self.max_samples)
            self.x_buffer = np.zeros(self.max_samples)
            self.y_buffer = np.zeros(self.max_samples)
            self.z_buffer = np.zeros(self.max_samples)
            
            # Copy back (trimmed if the new buffer is smaller)
            to_copy = min(len(old_t), self.max_samples)
            if to_copy > 0:
                self.time_buffer[:to_copy] = old_t[-to_copy:]
                self.x_buffer[:to_copy] = old_x[-to_copy:]
                self.y_buffer[:to_copy] = old_y[-to_copy:]
                self.z_buffer[:to_copy] = old_z[-to_copy:]
                self.ptr = to_copy % self.max_samples
            else:
                self.ptr = 0

            self.status_bar.showMessage(f"History resized to {new_size}. Preserved {to_copy} samples.")
            self.save_config()
        except ValueError:
            pass

    def load_config(self):
        if os.path.exists(CONFIG_FILE):
            try:
                with open(CONFIG_FILE, 'r') as f:
                    config = json.load(f)
                    index = self.port_combo.findText(config.get("port", ""))
                    if index >= 0:
                        self.port_combo.setCurrentIndex(index)
                    self.rate_input.setText(config.get("rate", "92"))
                    
                    hist_val = config.get("history", "2000")
                    self.history_combo.setCurrentText(hist_val)
                    self.update_buffer_size()
                    
                    fft_val = config.get("fft_window", "256")
                    self.nperseg_combo.setCurrentText(fft_val)
            except Exception:
                pass

    def save_config(self):
        config = {
            "port": self.port_combo.currentText(),
            "rate": self.rate_input.text(),
            "history": self.history_combo.currentText(),
            "fft_window": self.nperseg_combo.currentText()
        }
        try:
            with open(CONFIG_FILE, 'w') as f:
                json.dump(config, f)
        except Exception:
            pass

    def toggle_connection(self):
        if self.serial_thread and self.serial_thread.isRunning():
            self.serial_thread.stop()
            self.serial_thread.wait()
            self.connect_btn.setText("Connect")
        else:
            port = self.port_combo.currentText()
            if not port:
                self.status_bar.showMessage("No port selected!")
                return
            
            self.save_config()
            self.serial_thread = SerialWorker(port)
            self.serial_thread.data_received.connect(self.handle_data)
            self.serial_thread.status_message.connect(self.status_bar.showMessage)
            self.serial_thread.connection_status.connect(self.handle_connection_status)
            self.serial_thread.start()
            self.connect_btn.setText("Disconnect")

    @Slot(bool)
    def handle_connection_status(self, connected):
        if not connected:
            self.connect_btn.setText("Connect")

    @Slot(float, int, int, int)
    def handle_data(self, ts, x, y, z):
        self.time_buffer[self.ptr] = ts
        self.x_buffer[self.ptr] = x
        self.y_buffer[self.ptr] = y
        self.z_buffer[self.ptr] = z
        
        self.ptr = (self.ptr + 1) % self.max_samples

        # Throttle UI updates to ~30Hz
        if self.ptr % 2 == 0:
            # Reconstruct continuous data from ring buffer
            data_t = np.concatenate((self.time_buffer[self.ptr:], self.time_buffer[:self.ptr]))
            data_x = np.concatenate((self.x_buffer[self.ptr:], self.x_buffer[:self.ptr]))
            data_y = np.concatenate((self.y_buffer[self.ptr:], self.y_buffer[:self.ptr]))
            data_z = np.concatenate((self.z_buffer[self.ptr:], self.z_buffer[:self.ptr]))

            valid_mask = data_t > 0
            if np.any(valid_mask):
                t_valid = data_t[valid_mask]
                t_plot = (t_valid - t_valid[0]) / 1000000.0 # us to s
                self.curve_x.setData(t_plot, data_x[valid_mask])
                self.curve_y.setData(t_plot, data_y[valid_mask])
                self.curve_z.setData(t_plot, data_z[valid_mask])

    def update_psd(self):
        if not self.auto_psd_cb.isChecked() and self.sender() == self.psd_timer:
            return

        # Re-order data first
        data_t = np.concatenate((self.time_buffer[self.ptr:], self.time_buffer[:self.ptr]))
        data_x = np.concatenate((self.x_buffer[self.ptr:], self.x_buffer[:self.ptr]))
        data_y = np.concatenate((self.y_buffer[self.ptr:], self.y_buffer[:self.ptr]))
        data_z = np.concatenate((self.z_buffer[self.ptr:], self.z_buffer[:self.ptr]))

        # Calculate mask on rotated data
        valid_mask = data_t > 0
        n_valid = np.sum(valid_mask)
        
        try:
            nperseg = int(self.nperseg_combo.currentText())
        except ValueError:
            nperseg = 256

        if n_valid < nperseg:
            if self.sender() != self.psd_timer:
                self.status_bar.showMessage(f"Not enough data for PSD (need {nperseg} samples)")
            return

        # Filter to only valid data
        data_t = data_t[valid_mask]
        data_x = data_x[valid_mask]
        data_y = data_y[valid_mask]
        data_z = data_z[valid_mask]

        # Estimate sample rate
        dt_us = np.median(np.diff(data_t))
        if dt_us <= 0:
            return
        fs = 1000000.0 / dt_us

        # Calculate PSD
        for buf, curve in [(data_x, self.psd_curve_x),
                           (data_y, self.psd_curve_y),
                           (data_z, self.psd_curve_z)]:
            f, pxx = signal.welch(buf, fs, nperseg=nperseg)
            curve.setData(f, pxx)

    def send_mcu_command(self, cmd):
        if self.serial_thread and self.serial_thread.isRunning():
            self.serial_thread.send_command(cmd)
        else:
            self.status_bar.showMessage("Not connected!")

    def set_mcu_rate(self):
        rate_hex = self.rate_input.text()
        if rate_hex:
            self.send_mcu_command(f"RATE {rate_hex}")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())
