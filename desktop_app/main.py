import sys
import json
import os
import time
import struct
import numpy as np
from scipy import signal
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                             QHBoxLayout, QPushButton, QComboBox, QLabel, 
                             QTabWidget, QStatusBar, QLineEdit, QCheckBox, QFrame,
                             QFileDialog, QSpinBox, QMessageBox)
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
        # Recording state
        self.is_recording = False
        self.recorded_samples = 0
        self.log_file = None
        self.record_duration = 60.0
        self.record_start_time = 0.0
        self.record_timer = QTimer()
        self.record_timer.timeout.connect(self.update_recording_timer)

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

        ctrl_layout.addWidget(QLabel("Rate:"))
        self.rate_combo = QComboBox()
        self.rate_combo.setFixedWidth(130)
        self.populate_rates_for_sensor("RM3100")
        self.rate_combo.currentIndexChanged.connect(self.set_mcu_rate)
        ctrl_layout.addWidget(self.rate_combo)

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

        # Stats Panel for Channel Means
        stats_frame = QFrame()
        stats_frame.setFrameShape(QFrame.StyledPanel)
        stats_frame.setStyleSheet("""
            QFrame {
                background-color: rgba(128, 128, 128, 0.1);
                border: 1px solid rgba(128, 128, 128, 0.2);
                border-radius: 4px;
            }
        """)
        stats_layout = QHBoxLayout(stats_frame)
        stats_layout.setContentsMargins(15, 6, 15, 6)
        
        stats_title = QLabel("Channel Means:")
        stats_title.setStyleSheet("font-weight: bold;")
        stats_layout.addWidget(stats_title)
        
        stats_layout.addSpacing(20)
        self.mean_x_label = QLabel("X: 0.00")
        self.mean_x_label.setStyleSheet("color: #ef5350; font-weight: bold; font-size: 13px;")
        stats_layout.addWidget(self.mean_x_label)
        
        stats_layout.addSpacing(20)
        self.mean_y_label = QLabel("Y: 0.00")
        self.mean_y_label.setStyleSheet("color: #66bb6a; font-weight: bold; font-size: 13px;")
        stats_layout.addWidget(self.mean_y_label)
        
        stats_layout.addSpacing(20)
        self.mean_z_label = QLabel("Z: 0.00")
        self.mean_z_label.setStyleSheet("color: #42a5f5; font-weight: bold; font-size: 13px;")
        stats_layout.addWidget(self.mean_z_label)
        
        stats_layout.addStretch()
        main_layout.addWidget(stats_frame)

        # Content layout containing Tabs (left) and Sidebar (right)
        content_layout = QHBoxLayout()
        main_layout.addLayout(content_layout)

        # Tabs for Visualization
        self.tabs = QTabWidget()
        content_layout.addWidget(self.tabs, stretch=4)

        # Sidebar for Acquisition
        sidebar = QFrame()
        sidebar.setFrameShape(QFrame.StyledPanel)
        sidebar.setFixedWidth(260)
        sidebar.setStyleSheet("""
            QFrame {
                background-color: rgba(128, 128, 128, 0.05);
                border: 1px solid rgba(128, 128, 128, 0.15);
                border-radius: 6px;
            }
            QLabel {
                background: transparent;
                border: none;
            }
            QPushButton {
                padding: 6px;
            }
        """)
        
        sidebar_layout = QVBoxLayout(sidebar)
        sidebar_layout.setContentsMargins(12, 12, 12, 12)
        sidebar_layout.setSpacing(10)
        
        # Title
        title = QLabel("Data Acquisition")
        title.setStyleSheet("font-weight: bold; font-size: 14px; border-bottom: 1px solid rgba(128,128,128,0.2); padding-bottom: 4px;")
        sidebar_layout.addWidget(title)
        
        # File selector section
        sidebar_layout.addWidget(QLabel("Output File:"))
        file_layout = QHBoxLayout()
        self.file_path_input = QLineEdit("acquisition.npy")
        self.file_path_input.setToolTip("Path to the output NumPy Binary (.npy) file")
        file_layout.addWidget(self.file_path_input)
        
        browse_btn = QPushButton("...")
        browse_btn.setFixedWidth(30)
        browse_btn.clicked.connect(self.browse_file)
        file_layout.addWidget(browse_btn)
        sidebar_layout.addLayout(file_layout)
        
        # Auto-stop configuration
        auto_stop_layout = QHBoxLayout()
        self.auto_stop_cb = QCheckBox("Auto-stop:")
        self.auto_stop_cb.setChecked(False)
        self.auto_stop_cb.stateChanged.connect(self.toggle_auto_stop_input)
        auto_stop_layout.addWidget(self.auto_stop_cb)
        
        self.duration_spin = QSpinBox()
        self.duration_spin.setRange(1, 86400) # 1s to 24 hours
        self.duration_spin.setValue(60)
        self.duration_spin.setSuffix(" s")
        self.duration_spin.setEnabled(False)
        auto_stop_layout.addWidget(self.duration_spin)
        sidebar_layout.addLayout(auto_stop_layout)
        
        sidebar_layout.addSpacing(10)
        
        # Action Buttons
        self.start_rec_btn = QPushButton("Start Recording")
        self.start_rec_btn.setStyleSheet("""
            QPushButton {
                background-color: #2e7d32;
                color: white;
                font-weight: bold;
                border-radius: 4px;
            }
            QPushButton:disabled {
                background-color: rgba(46, 125, 50, 0.3);
                color: rgba(255, 255, 255, 0.5);
            }
        """)
        self.start_rec_btn.clicked.connect(self.start_recording)
        sidebar_layout.addWidget(self.start_rec_btn)
        
        self.stop_rec_btn = QPushButton("Stop Recording")
        self.stop_rec_btn.setStyleSheet("""
            QPushButton {
                background-color: #c62828;
                color: white;
                font-weight: bold;
                border-radius: 4px;
            }
            QPushButton:disabled {
                background-color: rgba(198, 40, 40, 0.3);
                color: rgba(255, 255, 255, 0.5);
            }
        """)
        self.stop_rec_btn.setEnabled(False)
        self.stop_rec_btn.clicked.connect(self.stop_recording)
        sidebar_layout.addWidget(self.stop_rec_btn)

        self.recover_rec_btn = QPushButton("Recover Interrupted (.tmp)...")
        self.recover_rec_btn.setToolTip("Recover data from an interrupted or crashed recording session (.tmp file)")
        self.recover_rec_btn.clicked.connect(self.recover_tmp_file_dialog)
        sidebar_layout.addWidget(self.recover_rec_btn)
        
        sidebar_layout.addSpacing(10)
        
        # Status block
        status_box = QFrame()
        status_box.setStyleSheet("background-color: rgba(0, 0, 0, 0.05); border-radius: 4px; border: none;")
        status_box_layout = QVBoxLayout(status_box)
        status_box_layout.setContentsMargins(8, 8, 8, 8)
        
        self.rec_status_label = QLabel("Status: Idle")
        self.rec_status_label.setStyleSheet("font-weight: bold; color: #aaaaaa;")
        status_box_layout.addWidget(self.rec_status_label)
        
        self.samples_label = QLabel("Recorded: 0 samples")
        status_box_layout.addWidget(self.samples_label)
        
        self.time_left_label = QLabel("Time left: --")
        status_box_layout.addWidget(self.time_left_label)
        
        sidebar_layout.addWidget(status_box)
        sidebar_layout.addStretch()
        
        content_layout.addWidget(sidebar, stretch=1)

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
                    rate_str = config.get("rate", "96")
                    try:
                        rate_val = int(rate_str, 16)
                        self.select_rate_code(rate_val)
                    except ValueError:
                        pass
                    
                    hist_val = config.get("history", "2000")
                    self.history_combo.setCurrentText(hist_val)
                    self.update_buffer_size()
                    
                    fft_val = config.get("fft_window", "256")
                    self.nperseg_combo.setCurrentText(fft_val)
            except Exception:
                pass

    def save_config(self):
        rate_code = self.rate_combo.currentData() if hasattr(self, 'rate_combo') else 0x96
        rate_hex = f"{rate_code:02x}" if rate_code is not None else "96"
        config = {
            "port": self.port_combo.currentText(),
            "rate": rate_hex,
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
            self.serial_thread.status_message.connect(self.handle_status_message)
            self.serial_thread.connection_status.connect(self.handle_connection_status)
            self.serial_thread.start()
            self.connect_btn.setText("Disconnect")

    @Slot(bool)
    def handle_connection_status(self, connected):
        if not connected:
            self.connect_btn.setText("Connect")
        else:
            self.connect_btn.setText("Disconnect")
            # Request MCU status after a 1.5 second delay (allows ESP32 auto-reset bootloader to pass)
            QTimer.singleShot(1500, lambda: self.send_mcu_command("STATUS"))

    @Slot(float, int, int, int)
    def handle_data(self, ts, x, y, z):
        if self.is_recording and self.log_file:
            try:
                # Store timestamp in seconds as 64-bit IEEE float (<d)
                ts_sec = float(ts) / 1000000.0
                self.log_file.write(struct.pack('<diii', ts_sec, x, y, z))
                self.recorded_samples += 1
                if self.recorded_samples % 10 == 0:
                    self.samples_label.setText(f"Recorded: {self.recorded_samples} samples")
            except Exception as e:
                self.status_bar.showMessage(f"Write error: {str(e)}")
                self.stop_recording()

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

                # Calculate and update channel means
                self.mean_x_label.setText(f"X: {np.mean(data_x[valid_mask]):.2f}")
                self.mean_y_label.setText(f"Y: {np.mean(data_y[valid_mask]):.2f}")
                self.mean_z_label.setText(f"Z: {np.mean(data_z[valid_mask]):.2f}")

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
        if not hasattr(self, 'rate_combo'):
            return
        rate_code = self.rate_combo.currentData()
        if rate_code is not None:
            rate_hex = f"{rate_code:02x}"
            self.send_mcu_command(f"RATE {rate_hex}")

    def populate_rates_for_sensor(self, sensor_name):
        self.rate_combo.blockSignals(True)
        self.rate_combo.clear()
        self.detected_sensor = sensor_name
        
        if sensor_name == "FLC100-ADS131E08":
            rates = [
                ("1 kSPS (06)", 0x06),
                ("2 kSPS (05)", 0x05),
                ("4 kSPS (04)", 0x04),
                ("8 kSPS (03)", 0x03),
                ("16 kSPS (02)", 0x02)
            ]
        else: # Default (RM3100)
            rates = [
                ("9 Hz (98)", 0x98),
                ("18 Hz (97)", 0x97),
                ("37 Hz (96)", 0x96),
                ("75 Hz (95)", 0x95),
                ("150 Hz (94)", 0x94),
                ("300 Hz (93)", 0x93),
                ("600 Hz (92)", 0x92)
            ]
            
        for label, code in rates:
            self.rate_combo.addItem(label, code)
            
        self.rate_combo.blockSignals(False)

    def select_rate_code(self, rate_code):
        self.rate_combo.blockSignals(True)
        index = self.rate_combo.findData(rate_code)
        if index >= 0:
            self.rate_combo.setCurrentIndex(index)
        self.rate_combo.blockSignals(False)

    def handle_status_message(self, msg):
        self.status_bar.showMessage(msg)
        
        if msg.startswith("MCU: "):
            line = msg[5:].strip().upper()
            
            # SENSOR: <Name>
            if line.startswith("SENSOR: "):
                orig_line = msg[5:].strip()
                sensor_name = orig_line[8:].strip()
                if not hasattr(self, 'detected_sensor') or self.detected_sensor != sensor_name:
                    self.populate_rates_for_sensor(sensor_name)
            
            # RATE CODE: 0X
            elif line.startswith("RATE CODE: 0X"):
                rate_str = line[13:].strip()
                try:
                    rate_val = int(rate_str, 16)
                    self.select_rate_code(rate_val)
                except ValueError:
                    pass

    def browse_file(self):
        filename, _ = QFileDialog.getSaveFileName(
            self,
            "Select Output File",
            self.file_path_input.text(),
            "NumPy Binary Files (*.npy);;All Files (*)"
        )
        if filename:
            self.file_path_input.setText(filename)

    def toggle_auto_stop_input(self, state):
        self.duration_spin.setEnabled(self.auto_stop_cb.isChecked())

    def start_recording(self):
        filepath = self.file_path_input.text().strip()
        if not filepath:
            self.status_bar.showMessage("Error: Please specify a valid filename.")
            return

        if os.path.exists(filepath):
            reply = QMessageBox.question(
                self,
                "Confirm Overwrite",
                f"The file '{os.path.basename(filepath)}' already exists. Do you want to overwrite it?",
                QMessageBox.Yes | QMessageBox.No,
                QMessageBox.No
            )
            if reply == QMessageBox.No:
                self.status_bar.showMessage("Recording cancelled (file exists).")
                return

        # Use a temporary file for raw binary streaming
        self.temp_filepath = filepath + ".tmp"

        # Check if an interrupted temp file already exists
        if os.path.exists(self.temp_filepath):
            reply = QMessageBox.question(
                self,
                "Interrupted Recording Found",
                f"An interrupted temporary recording was found:\n{self.temp_filepath}\n\nWould you like to recover it to a .npy file before starting?",
                QMessageBox.Yes | QMessageBox.No | QMessageBox.Cancel,
                QMessageBox.Yes
            )
            if reply == QMessageBox.Yes:
                rec_path = self.temp_filepath.rsplit('.tmp', 1)[0] + "_recovered.npy"
                n_samples, err = self.convert_tmp_to_npy(self.temp_filepath, rec_path)
                if err is None:
                    QMessageBox.information(self, "Recovery Successful", f"Recovered {n_samples:,} samples to:\n{rec_path}")
                else:
                    QMessageBox.warning(self, "Recovery Warning", f"Could not recover file: {err}")
            elif reply == QMessageBox.Cancel:
                return
        try:
            self.log_file = open(self.temp_filepath, "wb")
        except Exception as e:
            self.status_bar.showMessage(f"Error opening temp file: {str(e)}")
            return

        self.is_recording = True
        self.recorded_samples = 0
        self.record_start_time = time.time()

        # Update UI state
        self.start_rec_btn.setEnabled(False)
        self.stop_rec_btn.setEnabled(True)
        self.file_path_input.setEnabled(False)
        self.auto_stop_cb.setEnabled(False)
        self.duration_spin.setEnabled(False)

        self.rec_status_label.setText("Status: Recording...")
        self.rec_status_label.setStyleSheet("font-weight: bold; color: #66bb6a;")
        self.samples_label.setText("Recorded: 0 samples")

        if self.auto_stop_cb.isChecked():
            self.record_duration = float(self.duration_spin.value())
            self.time_left_label.setText(f"Time left: {self.record_duration:.1f}s")
            self.record_timer.start(100) # update every 100ms
        else:
            self.time_left_label.setText("Time left: Continuous")

        self.status_bar.showMessage(f"Recording started -> {os.path.basename(filepath)}")

    def stop_recording(self):
        if not hasattr(self, 'is_recording') or not self.is_recording:
            return

        self.is_recording = False
        self.record_timer.stop()

        # Close the temporary binary file
        try:
            if hasattr(self, 'log_file') and self.log_file:
                self.log_file.flush()
                self.log_file.close()
                self.log_file = None
        except Exception as e:
            self.status_bar.showMessage(f"Error closing temp file: {str(e)}")

        # Convert raw binary to structured .npy format
        filepath = self.file_path_input.text().strip()
        save_success = False
        try:
            if os.path.exists(self.temp_filepath):
                # Read structured binary data from temp file (time in seconds as float64)
                data = np.fromfile(
                    self.temp_filepath,
                    dtype=[('time_s', '<f8'), ('x', '<i4'), ('y', '<i4'), ('z', '<i4')]
                )
                # Save as standard NumPy .npy file
                np.save(filepath, data)
                save_success = True
        except Exception as e:
            self.status_bar.showMessage(f"Error compiling .npy file: {str(e)}")
        finally:
            # Clean up temp file
            if hasattr(self, 'temp_filepath') and os.path.exists(self.temp_filepath):
                try:
                    os.remove(self.temp_filepath)
                except Exception:
                    pass

        # Update UI state
        self.start_rec_btn.setEnabled(True)
        self.stop_rec_btn.setEnabled(False)
        self.file_path_input.setEnabled(True)
        self.auto_stop_cb.setEnabled(True)
        self.duration_spin.setEnabled(self.auto_stop_cb.isChecked())

        self.rec_status_label.setText("Status: Idle")
        self.rec_status_label.setStyleSheet("font-weight: bold; color: #aaaaaa;")
        self.time_left_label.setText("Time left: --")

        # Make sure the final count is written
        self.samples_label.setText(f"Recorded: {self.recorded_samples} samples")
        if save_success:
            self.status_bar.showMessage(f"NumPy Binary saved: {os.path.basename(filepath)} ({self.recorded_samples} samples)")

    def update_recording_timer(self):
        if not hasattr(self, 'is_recording') or not self.is_recording:
            self.record_timer.stop()
            return

        elapsed = time.time() - self.record_start_time
        remaining = max(0.0, self.record_duration - elapsed)

        self.time_left_label.setText(f"Time left: {remaining:.1f}s")

        if remaining <= 0:
            self.stop_recording()

    def convert_tmp_to_npy(self, tmp_filepath, npy_filepath):
        """Converts an interrupted raw binary .tmp file into a structured .npy dataset."""
        try:
            data = np.fromfile(
                tmp_filepath,
                dtype=[('time_s', '<f8'), ('x', '<i4'), ('y', '<i4'), ('z', '<i4')]
            )
            np.save(npy_filepath, data)
            return len(data), None
        except Exception as e:
            return 0, str(e)

    def recover_tmp_file_dialog(self):
        """Allows the user to manually select and recover any interrupted .tmp binary file into a .npy dataset."""
        default_tmp = self.file_path_input.text().strip() + ".tmp"
        initial_dir = default_tmp if os.path.exists(default_tmp) else ""
        tmp_filepath, _ = QFileDialog.getOpenFileName(
            self,
            "Select Interrupted Recording (.tmp)",
            initial_dir,
            "Temporary Binary Files (*.tmp);;All Files (*)"
        )
        if not tmp_filepath:
            return

        default_out = tmp_filepath.rsplit('.tmp', 1)[0]
        if not default_out.endswith('.npy'):
            default_out += '.npy'

        npy_filepath, _ = QFileDialog.getSaveFileName(
            self,
            "Save Recovered NumPy File",
            default_out,
            "NumPy Binary Files (*.npy);;All Files (*)"
        )
        if not npy_filepath:
            return

        n_samples, err = self.convert_tmp_to_npy(tmp_filepath, npy_filepath)
        if err is None:
            QMessageBox.information(
                self,
                "Recovery Successful",
                f"Successfully recovered {n_samples:,} samples to:\n{npy_filepath}"
            )
            self.status_bar.showMessage(f"Recovered {n_samples} samples -> {os.path.basename(npy_filepath)}")
        else:
            QMessageBox.critical(self, "Recovery Error", f"Failed to recover file:\n{err}")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())
