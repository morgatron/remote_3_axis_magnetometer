import socket
from PySide6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QFormLayout, 
                             QLabel, QLineEdit, QComboBox, QSpinBox, QPushButton, QMessageBox)

class ProvisionDialog(QDialog):
    """
    Modal dialog for provisioning ESP32 NVS non-volatile settings over Serial CDC.
    """
    def __init__(self, parent=None, command_sender=None, current_rate_hex="95", current_cycle=200):
        super().__init__(parent)
        self.command_sender = command_sender
        self.setWindowTitle("Provision Remote Node (ESP32 NVS Setup)")
        self.setFixedWidth(420)

        layout = QVBoxLayout(self)
        form = QFormLayout()

        # Output Mode
        self.mode_combo = QComboBox()
        self.mode_combo.addItem("SERIAL (USB Testing Mode)", "SERIAL")
        self.mode_combo.addItem("WIFI (Remote UDP Burst Mode)", "WIFI")
        self.mode_combo.addItem("BLE (Bluetooth LE Long Range)", "BLE")
        self.mode_combo.addItem("BOTH (Simultaneous USB & WiFi)", "BOTH")
        form.addRow("Output Mode:", self.mode_combo)

        # Sensor Hardware Model
        self.sensor_combo = QComboBox()
        self.sensor_combo.addItem("FLC100-ADS131E08 (24-bit Analog Fluxgate)", "FLC100")
        self.sensor_combo.addItem("PNI RM3100 (Digital SPI Magnetometer)", "RM3100")
        form.addRow("Sensor Hardware:", self.sensor_combo)

        # Device ID
        self.device_id_input = QLineEdit()
        self.device_id_input.setPlaceholderText("e.g. SENSOR_01 (Leave blank for MAC default)")
        form.addRow("Device ID:", self.device_id_input)

        # Sampling Rate
        self.rate_combo = QComboBox()
        rates_flc = [("1000 Hz / 1 kS/s", "06"), ("500 Hz", "05"), ("250 Hz", "FA"), ("100 Hz", "64"), ("50 Hz", "32"), ("10 Hz", "0A")]
        rates_rm = [("37 Hz", "96"), ("75 Hz", "95"), ("150 Hz", "94"), ("300 Hz", "93"), ("600 Hz", "92"), ("18 Hz", "97"), ("9 Hz", "98")]

        for label, val in rates_flc:
            self.rate_combo.addItem(f"FLC100: {label}", ("FLC100", val))
        for label, val in rates_rm:
            self.rate_combo.addItem(f"RM3100: {label}", ("RM3100", val))

        for i in range(self.rate_combo.count()):
            if self.rate_combo.itemData(i)[1].upper() == current_rate_hex.upper():
                self.rate_combo.setCurrentIndex(i)
                break
        form.addRow("Sampling Rate:", self.rate_combo)

        # Software Downsample (FLC100)
        self.ds_spin = QSpinBox()
        self.ds_spin.setRange(1, 100)
        self.ds_spin.setValue(1)
        form.addRow("Downsample Factor (FLC100):", self.ds_spin)

        # PGA Gain (FLC100)
        self.gain_combo = QComboBox()
        for g in [1, 2, 4, 8]:
            self.gain_combo.addItem(f"{g}x", g)
        form.addRow("PGA Gain (FLC100):", self.gain_combo)

        # RM3100 Cycle Count
        self.cycle_spin = QSpinBox()
        self.cycle_spin.setRange(50, 1000)
        self.cycle_spin.setSingleStep(10)
        self.cycle_spin.setValue(current_cycle)
        form.addRow("Cycle Count (RM3100):", self.cycle_spin)

        # WiFi SSID
        self.ssid_input = QLineEdit()
        self.ssid_input.setPlaceholderText("Network SSID")
        form.addRow("WiFi SSID:", self.ssid_input)

        # WiFi Password
        self.pass_input = QLineEdit()
        self.pass_input.setEchoMode(QLineEdit.Password)
        self.pass_input.setPlaceholderText("Network Password")
        form.addRow("WiFi Password:", self.pass_input)

        # Target IP
        local_ip = "255.255.255.255"
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            local_ip = s.getsockname()[0]
            s.close()
        except Exception:
            pass

        self.target_input = QLineEdit(local_ip)
        self.target_input.setPlaceholderText("e.g. 192.168.1.100")
        form.addRow("Target Server IP:", self.target_input)

        layout.addLayout(form)

        # Buttons
        btn_box = QHBoxLayout()
        apply_btn = QPushButton("Apply & Save to ESP32 NVS")
        apply_btn.setStyleSheet("background-color: #2e7d32; color: white; font-weight: bold; padding: 6px;")
        apply_btn.clicked.connect(self._apply_provisioning)
        btn_box.addWidget(apply_btn)

        cancel_btn = QPushButton("Cancel")
        cancel_btn.clicked.connect(self.reject)
        btn_box.addWidget(cancel_btn)

        layout.addLayout(btn_box)

    def _apply_provisioning(self):
        mode_val = self.mode_combo.currentData()
        sensor_val = self.sensor_combo.currentData()
        dev_id_val = self.device_id_input.text().strip()
        ssid_val = self.ssid_input.text().strip()
        pass_val = self.pass_input.text().strip()
        target_val = self.target_input.text().strip()

        if mode_val in ["WIFI", "BOTH"] and not ssid_val:
            QMessageBox.warning(self, "Missing WiFi SSID", "Please enter a valid WiFi SSID for WiFi mode.")
            return

        if self.command_sender:
            self.command_sender(f"SENSOR {sensor_val}")
            if dev_id_val:
                self.command_sender(f"ID {dev_id_val}")

            rate_info = self.rate_combo.itemData(self.rate_combo.currentIndex())
            if rate_info:
                self.command_sender(f"RATE {rate_info[1]}")

            if sensor_val == "FLC100":
                self.command_sender(f"DOWNSAMPLE {self.ds_spin.value()}")
                self.command_sender(f"GAIN {self.gain_combo.currentData()}")
            elif sensor_val == "RM3100":
                self.command_sender(f"CYCLE {self.cycle_spin.value()}")

            if ssid_val and pass_val:
                self.command_sender(f"WIFI {ssid_val} {pass_val}")
            if target_val:
                self.command_sender(f"TARGET {target_val}")

            self.command_sender(f"MODE {mode_val}")

        QMessageBox.information(
            self,
            "Provisioning Saved",
            f"Node provisioned successfully!\n\n- Output Mode: {mode_val}\n- Device ID: {dev_id_val or 'MAC Default'}\n- Target IP: {target_val}\n\nAll parameters saved to ESP32 NVS Flash."
        )
        self.accept()
