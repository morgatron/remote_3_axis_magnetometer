import numpy as np
from scipy import signal
from PySide6.QtWidgets import QWidget, QVBoxLayout, QPushButton
from PySide6.QtCore import Signal
import pyqtgraph as pg

class PsdPlot(QWidget):
    """
    Power Spectral Density (PSD) analysis widget using scipy.signal.welch.
    """
    update_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        self.plot_widget = pg.PlotWidget(title="Power Spectral Density (Welch)")
        self.plot_widget.addLegend()
        self.plot_widget.setLabel('left', 'Power/Frequency (dB/Hz)')
        self.plot_widget.setLabel('bottom', 'Frequency (Hz)')
        self.plot_widget.setLogMode(x=False, y=True)
        self.plot_widget.showGrid(x=True, y=True)

        self.curve_x = self.plot_widget.plot(pen='r', name='X')
        self.curve_y = self.plot_widget.plot(pen='g', name='Y')
        self.curve_z = self.plot_widget.plot(pen='b', name='Z')

        layout.addWidget(self.plot_widget)

        self.calc_btn = QPushButton("Update PSD Now")
        self.calc_btn.clicked.connect(self.update_requested.emit)
        layout.addWidget(self.calc_btn)

    def calculate_and_plot(self, data_t: np.ndarray, data_x: np.ndarray, data_y: np.ndarray, data_z: np.ndarray, nperseg: int = 256):
        """Computes Welch periodogram and updates PSD curves."""
        if len(data_t) < nperseg or len(data_t) < 2:
            return False

        # Estimate sample rate from time differences
        dt_us = np.median(np.diff(data_t))
        if dt_us <= 0:
            return False
        fs = 1000000.0 / dt_us

        # Calculate PSD for each axis
        for buf, curve in [(data_x, self.curve_x),
                           (data_y, self.curve_y),
                           (data_z, self.curve_z)]:
            f, pxx = signal.welch(buf, fs, nperseg=nperseg)
            curve.setData(f, pxx)

        return True
