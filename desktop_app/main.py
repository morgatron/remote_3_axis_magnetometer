#!/usr/bin/env python3
"""
Remote 3-Axis Magnetometer Desktop Visualization Application

Modular architecture orchestrating PyQtGraph plotting, PSD analysis,
HDF5 streaming data acquisition, and ESP32 node provisioning.
"""

import sys
import json
import os
import time
import numpy as np

from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                             QHBoxLayout, QPushButton, QComboBox, QLabel, 
                             QTabWidget, QStatusBar, QCheckBox, QMessageBox)
from PySide6.QtCore import Slot, QTimer
import serial.tools.list_ports

from core.data_buffer import DataBuffer
from core.hdf5_recorder import Hdf5Recorder
from widgets.time_series_plot import TimeSeriesPlot
from widgets.psd_plot import PsdPlot
from widgets.stats_panel import StatsPanel
from widgets.acquisition_sidebar import AcquisitionSidebar
from widgets.provision_dialog import ProvisionDialog
from serial_worker import SerialWorker
from udp_worker import UdpWorker

CONFIG_FILE = os.path.join(os.path.dirname(__file__), "config.json")

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("3-Axis Magnetometer Visualizer")
        self.resize(1020, 720)

        # Core Data Storage & State
        self.data_buffer = DataBuffer(max_samples=2000)
        self.recorder = None
        self.serial_thread = None
        self.discovered_nodes = set(["All Nodes"])
        self.detected_sensor = "RM3100"
        self.active_cycle_count = 200

        # Recording timer for auto-stop
        self.record_timer = QTimer()
        self.record_timer.timeout.connect(self._update_recording_timer)

        self._init_ui()
        self._load_config()

        # Timer for Auto-PSD
        self.psd_timer = QTimer()
        self.psd_timer.timeout.connect(self._update_psd)
        self.psd_timer.start(1000)

    def _init_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)

        # 1. Connection & Node Toolbar
        conn_layout = QHBoxLayout()
        conn_layout.addWidget(QLabel("Port:"))
        self.port_combo = QComboBox()
        self.refresh_ports()
        conn_layout.addWidget(self.port_combo)

        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self.toggle_connection)
        conn_layout.addWidget(self.connect_btn)

        refresh_btn = QPushButton("Refresh")
        refresh_btn.clicked.connect(self.refresh_ports)
        conn_layout.addWidget(refresh_btn)

        conn_layout.addSpacing(15)
        conn_layout.addWidget(QLabel("Active Node:"))
        self.node_combo = QComboBox()
        self.node_combo.addItem("All Nodes")
        conn_layout.addWidget(self.node_combo)

        provision_btn = QPushButton("Provision Node...")
        provision_btn.setToolTip("Configure WiFi, streaming mode, target IP, and Device ID for remote deployment")
        provision_btn.clicked.connect(self.open_provision_dialog)
        conn_layout.addWidget(provision_btn)

        conn_layout.addStretch()
        main_layout.addLayout(conn_layout)

        # 2. Control Toolbar
        ctrl_layout = QHBoxLayout()
        self.stream_btn = QPushButton("Stream ON")
        self.stream_btn.clicked.connect(lambda: self.send_mcu_command("STREAM ON"))
        ctrl_layout.addWidget(self.stream_btn)

        self.stop_stream_btn = QPushButton("Stream OFF")
        self.stop_stream_btn.clicked.connect(lambda: self.send_mcu_command("STREAM OFF"))
        ctrl_layout.addWidget(self.stop_stream_btn)

        ctrl_layout.addSpacing(15)
        ctrl_layout.addWidget(QLabel("Sensor Model:"))
        self.sensor_type_combo = QComboBox()
        self.sensor_type_combo.addItem("Auto-Detect", "AUTO")
        self.sensor_type_combo.addItem("PNI RM3100 (Digital SPI)", "RM3100")
        self.sensor_type_combo.addItem("FLC100-ADS131E08 (24-bit Analog)", "FLC100")
        self.sensor_type_combo.currentIndexChanged.connect(self._on_user_select_sensor_type)
        ctrl_layout.addWidget(self.sensor_type_combo)

        ctrl_layout.addSpacing(15)
        ctrl_layout.addWidget(QLabel("Rate:"))
        self.rate_combo = QComboBox()
        self.rate_combo.setFixedWidth(160)
        self.rate_combo.currentIndexChanged.connect(self._set_mcu_rate)
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
        self.history_combo.currentTextChanged.connect(self._update_buffer_size)
        ctrl_layout.addWidget(self.history_combo)

        self.auto_psd_cb = QCheckBox("Auto-Update PSD")
        self.auto_psd_cb.setChecked(True)
        ctrl_layout.addWidget(self.auto_psd_cb)

        ctrl_layout.addSpacing(20)
        ctrl_layout.addWidget(QLabel("Live Filter:"))
        self.filter_combo = QComboBox()
        for val in ["Off (Raw)", "Low-Pass 50Hz", "Low-Pass 10Hz"]:
            self.filter_combo.addItem(val)
        self.filter_combo.setCurrentText("Off (Raw)")
        ctrl_layout.addWidget(self.filter_combo)

        main_layout.addLayout(ctrl_layout)

        # 3. Channel Means Statistics Panel
        self.stats_panel = StatsPanel()
        main_layout.addWidget(self.stats_panel)

        # 4. Content Area: Tabs (Left) + Acquisition Sidebar (Right)
        content_layout = QHBoxLayout()
        main_layout.addLayout(content_layout)

        self.tabs = QTabWidget()
        self.time_plot = TimeSeriesPlot()
        self.psd_plot = PsdPlot()
        self.psd_plot.update_requested.connect(self._update_psd)

        self.tabs.addTab(self.time_plot, "Time Series")
        self.tabs.addTab(self.psd_plot, "PSD Analysis")
        content_layout.addWidget(self.tabs, stretch=4)

        # Sidebar
        self.sidebar = AcquisitionSidebar()
        self.sidebar.start_recording_requested.connect(self.start_recording)
        self.sidebar.stop_recording_requested.connect(self.stop_recording)
        self.sidebar.command_requested.connect(self.send_mcu_command)
        content_layout.addWidget(self.sidebar, stretch=1)

        # Status Bar
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)

        self.populate_rates_for_sensor("RM3100")

    def refresh_ports(self):
        self.port_combo.clear()
        self.port_combo.addItem("WIFI_UDP (Port 9876)")
        ports = serial.tools.list_ports.comports()
        for p in ports:
            if any(pattern in p.device for pattern in ["ttyUSB", "ttyACM", "COM"]):
                self.port_combo.addItem(p.device)
        if self.port_combo.findText("MOCK_SENSOR") == -1:
            self.port_combo.addItem("MOCK_SENSOR")

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

            self._save_config()
            if port.startswith("WIFI_UDP"):
                self.serial_thread = UdpWorker(listen_port=9876)
            else:
                self.serial_thread = SerialWorker(port)

            self.serial_thread.data_received.connect(self.handle_data)
            self.serial_thread.status_message.connect(self.handle_status_message)
            self.serial_thread.connection_status.connect(self.handle_connection_status)
            self.serial_thread.start()
            self.connect_btn.setText("Disconnect")

    @Slot(bool)
    def handle_connection_status(self, connected: bool):
        self.connect_btn.setText("Disconnect" if connected else "Connect")
        if connected:
            QTimer.singleShot(1500, lambda: self.send_mcu_command("STATUS"))

    @Slot(str, object, object, object, object, object)
    def handle_data(self, device_id: str, ts: int, x: float, y: float, z: float, status: int = 0xC00000):
        if device_id not in self.discovered_nodes:
            self.discovered_nodes.add(device_id)
            self.node_combo.addItem(device_id)

        selected_node = self.node_combo.currentText()
        if selected_node != "All Nodes" and device_id != selected_node:
            return

        # Record to HDF5 if active
        if self.recorder and self.recorder.is_recording:
            try:
                count = self.recorder.add_sample(ts, x, y, z, status)
                if count % 10 == 0:
                    self.sidebar.samples_label.setText(f"Recorded: {count} samples")
            except Exception as e:
                self.status_bar.showMessage(f"HDF5 stream error: {e}")
                self.stop_recording()

        self.data_buffer.add_sample(ts, x, y, z, status)

        # Throttle GUI rendering to 50ms interval (20 FPS)
        now = time.perf_counter()
        if not hasattr(self, 'last_plot_time') or (now - self.last_plot_time) >= 0.05:
            self.last_plot_time = now
            t_plot, x_arr, y_arr, z_arr, _ = self.data_buffer.get_ordered_data()
            if len(t_plot) > 0:
                filt_mode = self.filter_combo.currentText()
                self.time_plot.update_data(t_plot, x_arr, y_arr, z_arr, filter_mode=filt_mode)

                unit_str = "nT"
                self.time_plot.set_unit_label(selected_node, unit_str)
                self.stats_panel.update_means(np.mean(x_arr), np.mean(y_arr), np.mean(z_arr), unit_str)

    def _update_psd(self):
        if not self.auto_psd_cb.isChecked() and self.sender() == self.psd_timer:
            return
        t_raw, x_raw, y_raw, z_raw = self.data_buffer.get_raw_valid_data()
        try:
            nperseg = int(self.nperseg_combo.currentText())
        except ValueError:
            nperseg = 256

        success = self.psd_plot.calculate_and_plot(t_raw, x_raw, y_raw, z_raw, nperseg=nperseg)
        if not success and self.sender() != self.psd_timer:
            self.status_bar.showMessage(f"Not enough data for PSD (need {nperseg} samples)")

    def start_recording(self):
        filepath = self.sidebar.file_path_input.text().strip()
        if not filepath:
            self.status_bar.showMessage("Error: Specify a valid filename.")
            return

        if os.path.exists(filepath):
            reply = QMessageBox.question(
                self, "Confirm Overwrite",
                f"File '{os.path.basename(filepath)}' already exists. Overwrite?",
                QMessageBox.Yes | QMessageBox.No, QMessageBox.No
            )
            if reply == QMessageBox.No:
                return

        sensor_type = self.sensor_type_combo.currentData()
        rate_code = self.rate_combo.currentData() or 0x95
        attrs = {
            'sensor_type': sensor_type,
            'sensor_model_detected': self.detected_sensor,
            'rate_code_hex': f"0x{rate_code:02x}",
            'rate_code_dec': int(rate_code)
        }

        if sensor_type == "RM3100" or "RM3100" in self.detected_sensor.upper():
            nc = self.sidebar.cycle_spin.value()
            gain = 0.3671 * float(nc) + 1.5
            attrs['cycle_count'] = nc
            attrs['gain_lsb_per_ut'] = gain
            attrs['scale_factor_nt_per_count'] = 1000.0 / gain
            attrs['data_units'] = "raw counts"
        else:
            attrs['downsample_factor'] = self.sidebar.downsample_combo.currentData() or 1
            attrs['pga_gain'] = self.sidebar.gain_combo.currentData() or 1
            attrs['vref_v'] = 2.4
            attrs['data_units'] = "raw 24-bit ADC counts"

        self.recorder = Hdf5Recorder(filepath, attrs=attrs)
        self.sidebar.set_recording_state(True)

        if self.sidebar.auto_stop_cb.isChecked():
            self.record_duration = float(self.sidebar.duration_spin.value())
            self.record_start_time = time.time()
            self.record_timer.start(100)
        else:
            self.sidebar.time_left_label.setText("Time left: Continuous")

        self.status_bar.showMessage(f"Recording HDF5 -> {os.path.basename(filepath)}")

    def stop_recording(self):
        if not self.recorder or not self.recorder.is_recording:
            return
        self.record_timer.stop()
        total_samples = self.recorder.close()
        self.sidebar.set_recording_state(False)
        self.sidebar.samples_label.setText(f"Recorded: {total_samples} samples")
        self.status_bar.showMessage(f"HDF5 saved: {os.path.basename(self.sidebar.file_path_input.text())} ({total_samples} samples)")

    def _update_recording_timer(self):
        if not self.recorder or not self.recorder.is_recording:
            self.record_timer.stop()
            return
        elapsed = time.time() - self.record_start_time
        remaining = max(0.0, self.record_duration - elapsed)
        self.sidebar.time_left_label.setText(f"Time left: {remaining:.1f}s")
        if remaining <= 0:
            self.stop_recording()

    def send_mcu_command(self, cmd: str):
        if self.serial_thread and self.serial_thread.isRunning():
            self.serial_thread.send_command(cmd)
        else:
            self.status_bar.showMessage("Not connected!")

    def open_provision_dialog(self):
        rate_code = self.rate_combo.currentData() or 0x95
        cycle = self.sidebar.cycle_spin.value()
        dlg = ProvisionDialog(
            self, command_sender=self.send_mcu_command,
            current_rate_hex=f"{rate_code:02x}", current_cycle=cycle
        )
        dlg.exec()

    def populate_rates_for_sensor(self, sensor_name: str):
        self.rate_combo.blockSignals(True)
        self.rate_combo.clear()
        self.detected_sensor = sensor_name
        self.sidebar.set_sensor_type(sensor_name)

        if "FLC100" in sensor_name:
            rates = [("10 Hz (0A)", 0x0A), ("50 Hz (32)", 0x32), ("100 Hz (64)", 0x64),
                     ("250 Hz (FA)", 0xFA), ("500 Hz (05)", 0x05), ("1000 Hz / 1 kS/s (06)", 0x06)]
        else:
            rates = [("9 Hz (98)", 0x98), ("18 Hz (97)", 0x97), ("37 Hz (96)", 0x96),
                     ("75 Hz (95)", 0x95), ("150 Hz (94)", 0x94), ("300 Hz (93)", 0x93), ("600 Hz (92)", 0x92)]

        for label, code in rates:
            self.rate_combo.addItem(label, code)
        self.rate_combo.blockSignals(False)

    def _set_mcu_rate(self):
        rate_code = self.rate_combo.currentData()
        if rate_code is not None:
            self.send_mcu_command(f"RATE {rate_code:02x}")

    def _on_user_select_sensor_type(self):
        val = self.sensor_type_combo.currentData()
        if val == "RM3100":
            self.populate_rates_for_sensor("RM3100")
            self.send_mcu_command("SENSOR RM3100")
        elif val == "FLC100":
            self.populate_rates_for_sensor("FLC100-ADS131E08")
            self.send_mcu_command("SENSOR FLC100")
        elif val == "AUTO":
            self.send_mcu_command("STATUS")

    def handle_status_message(self, msg: str):
        self.status_bar.showMessage(msg)
        line_upper = msg.upper()
        if "RM3100" in line_upper:
            self.populate_rates_for_sensor("RM3100")
        elif "FLC100" in line_upper or "ADS131" in line_upper:
            self.populate_rates_for_sensor("FLC100-ADS131E08")

    def _update_buffer_size(self):
        try:
            sz = int(self.history_combo.currentText())
            copied = self.data_buffer.resize(sz)
            self.status_bar.showMessage(f"History resized to {sz}. Preserved {copied} samples.")
            self._save_config()
        except ValueError:
            pass

    def _load_config(self):
        if os.path.exists(CONFIG_FILE):
            try:
                with open(CONFIG_FILE, 'r') as f:
                    cfg = json.load(f)
                    idx = self.port_combo.findText(cfg.get("port", ""))
                    if idx >= 0:
                        self.port_combo.setCurrentIndex(idx)
                    self.history_combo.setCurrentText(cfg.get("history", "2000"))
                    self._update_buffer_size()
                    self.nperseg_combo.setCurrentText(cfg.get("fft_window", "256"))
            except Exception:
                pass

    def _save_config(self):
        cfg = {
            "port": self.port_combo.currentText(),
            "history": self.history_combo.currentText(),
            "fft_window": self.nperseg_combo.currentText()
        }
        try:
            with open(CONFIG_FILE, 'w') as f:
                json.dump(cfg, f)
        except Exception:
            pass

    def closeEvent(self, event):
        if self.serial_thread and self.serial_thread.isRunning():
            self.serial_thread.stop()
            self.serial_thread.wait(1000)
        event.accept()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())
