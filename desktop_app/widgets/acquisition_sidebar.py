from PySide6.QtWidgets import (QFrame, QVBoxLayout, QHBoxLayout, QLabel, 
                             QLineEdit, QPushButton, QCheckBox, QSpinBox, 
                             QGroupBox, QStackedWidget, QWidget, QComboBox, QFileDialog)
from PySide6.QtCore import Signal

class AcquisitionSidebar(QFrame):
    """
    Sidebar widget managing HDF5 data acquisition, auto-stop timing,
    and dynamic hardware controls (RM3100 cycle count vs. FLC100 gain/downsample).
    """
    start_recording_requested = Signal()
    stop_recording_requested = Signal()
    command_requested = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFrameShape(QFrame.StyledPanel)
        self.setFixedWidth(260)
        self.setStyleSheet("""
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

        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.setSpacing(10)

        # Title
        title = QLabel("Data Acquisition")
        title.setStyleSheet("font-weight: bold; font-size: 14px; border-bottom: 1px solid rgba(128,128,128,0.2); padding-bottom: 4px;")
        layout.addWidget(title)

        # File selector section
        layout.addWidget(QLabel("Output File:"))
        file_layout = QHBoxLayout()
        self.file_path_input = QLineEdit("acquisition.h5")
        self.file_path_input.setToolTip("Path to the output HDF5 (.h5) dataset file")
        file_layout.addWidget(self.file_path_input)

        browse_btn = QPushButton("...")
        browse_btn.setFixedWidth(30)
        browse_btn.clicked.connect(self._browse_file)
        file_layout.addWidget(browse_btn)
        layout.addLayout(file_layout)

        # Auto-stop configuration
        auto_stop_layout = QHBoxLayout()
        self.auto_stop_cb = QCheckBox("Auto-stop:")
        self.auto_stop_cb.setChecked(False)
        self.auto_stop_cb.stateChanged.connect(lambda: self.duration_spin.setEnabled(self.auto_stop_cb.isChecked()))
        auto_stop_layout.addWidget(self.auto_stop_cb)

        self.duration_spin = QSpinBox()
        self.duration_spin.setRange(1, 86400)  # 1s to 24 hours
        self.duration_spin.setValue(60)
        self.duration_spin.setSuffix(" s")
        self.duration_spin.setEnabled(False)
        auto_stop_layout.addWidget(self.duration_spin)
        layout.addLayout(auto_stop_layout)

        layout.addSpacing(10)

        # Dynamic Sensor Control Box
        self.sensor_ctrl_box = QGroupBox("Sensor Controls")
        sensor_box_layout = QVBoxLayout(self.sensor_ctrl_box)
        sensor_box_layout.setContentsMargins(6, 6, 6, 6)

        self.sensor_stack = QStackedWidget()

        # Page 0: FLC100-ADS131 Controls
        flc_page = QWidget()
        flc_layout = QVBoxLayout(flc_page)
        flc_layout.setContentsMargins(0, 0, 0, 0)
        flc_layout.setSpacing(4)

        flc_layout.addWidget(QLabel("Software Downsample:"))
        self.downsample_combo = QComboBox()
        for label, val in [("1x (Raw 1 kS/s)", 1), ("2x (500 Hz)", 2), ("4x (250 Hz)", 4), ("10x (100 Hz)", 10), ("20x (50 Hz)", 20), ("100x (10 Hz)", 100)]:
            self.downsample_combo.addItem(label, val)
        self.downsample_combo.currentIndexChanged.connect(self._on_downsample_changed)
        flc_layout.addWidget(self.downsample_combo)

        flc_layout.addWidget(QLabel("PGA Gain:"))
        self.gain_combo = QComboBox()
        for gain in [1, 2, 4, 8]:
            self.gain_combo.addItem(f"{gain}x", gain)
        self.gain_combo.currentIndexChanged.connect(self._on_gain_changed)
        flc_layout.addWidget(self.gain_combo)

        self.test_sig_cb = QCheckBox("1 Hz Test Signal")
        self.test_sig_cb.stateChanged.connect(lambda state: self.command_requested.emit("TEST ON" if state else "TEST OFF"))
        flc_layout.addWidget(self.test_sig_cb)

        self.sensor_stack.addWidget(flc_page)  # Index 0

        # Page 1: RM3100 Controls
        rm_page = QWidget()
        rm_layout = QVBoxLayout(rm_page)
        rm_layout.setContentsMargins(0, 0, 0, 0)
        rm_layout.setSpacing(4)

        rm_layout.addWidget(QLabel("Cycle Count (Cycles):"))
        self.cycle_spin = QSpinBox()
        self.cycle_spin.setRange(50, 1000)
        self.cycle_spin.setValue(200)
        self.cycle_spin.setSingleStep(10)
        self.cycle_spin.valueChanged.connect(self._on_cycle_changed)
        rm_layout.addWidget(self.cycle_spin)

        self.sensor_stack.addWidget(rm_page)  # Index 1

        sensor_box_layout.addWidget(self.sensor_stack)
        layout.addWidget(self.sensor_ctrl_box)

        layout.addSpacing(10)

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
        self.start_rec_btn.clicked.connect(self.start_recording_requested.emit)
        layout.addWidget(self.start_rec_btn)

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
        self.stop_rec_btn.clicked.connect(self.stop_recording_requested.emit)
        layout.addWidget(self.stop_rec_btn)

        layout.addSpacing(10)

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

        layout.addWidget(status_box)
        layout.addStretch()

    def set_sensor_type(self, sensor_name: str):
        """Switches sensor control page (FLC100 vs RM3100)."""
        if "FLC100" in sensor_name:
            self.sensor_stack.setCurrentIndex(0)
        else:
            self.sensor_stack.setCurrentIndex(1)

    def set_recording_state(self, recording: bool):
        """Updates UI buttons and input fields based on active recording state."""
        self.start_rec_btn.setEnabled(not recording)
        self.stop_rec_btn.setEnabled(recording)
        self.file_path_input.setEnabled(not recording)
        self.auto_stop_cb.setEnabled(not recording)
        self.duration_spin.setEnabled(not recording and self.auto_stop_cb.isChecked())

        if recording:
            self.rec_status_label.setText("Status: Recording...")
            self.rec_status_label.setStyleSheet("font-weight: bold; color: #66bb6a;")
        else:
            self.rec_status_label.setText("Status: Idle")
            self.rec_status_label.setStyleSheet("font-weight: bold; color: #aaaaaa;")
            self.time_left_label.setText("Time left: --")

    def _browse_file(self):
        filename, _ = QFileDialog.getSaveFileName(
            self,
            "Select Output File",
            self.file_path_input.text(),
            "HDF5 Scientific Files (*.h5 *.hdf5);;All Files (*)"
        )
        if filename:
            self.file_path_input.setText(filename)

    def _on_downsample_changed(self):
        val = self.downsample_combo.currentData()
        if val is not None:
            self.command_requested.emit(f"DOWNSAMPLE {val}")

    def _on_gain_changed(self):
        gain = self.gain_combo.currentData()
        if gain is not None:
            self.command_requested.emit(f"GAIN {gain}")

    def _on_cycle_changed(self, count: int):
        self.command_requested.emit(f"CYCLE {count}")
