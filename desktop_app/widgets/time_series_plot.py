import numpy as np
from PySide6.QtWidgets import QWidget, QVBoxLayout
import pyqtgraph as pg

class TimeSeriesPlot(QWidget):
    """
    Real-time 3-axis time-series visualization widget using PyQtGraph.
    """
    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        self.plot_widget = pg.PlotWidget(title="Real-time Magnetometer Data")
        self.plot_widget.addLegend()
        self.plot_widget.setLabel('left', 'Magnetic Field (nT)')
        self.plot_widget.setLabel('bottom', 'Time (s)')
        self.plot_widget.showGrid(x=True, y=True)

        self.curve_x = self.plot_widget.plot(pen='r', name='X')
        self.curve_y = self.plot_widget.plot(pen='g', name='Y')
        self.curve_z = self.plot_widget.plot(pen='b', name='Z')

        layout.addWidget(self.plot_widget)

    def set_unit_label(self, node_id: str, unit_str: str):
        """Updates the left axis label with active node and units."""
        self.plot_widget.setLabel('left', f'[{node_id}] Field ({unit_str})')

    def update_data(self, t_plot: np.ndarray, x: np.ndarray, y: np.ndarray, z: np.ndarray, filter_mode: str = "Off (Raw)"):
        """Applies real-time smoothing filter and renders the curves."""
        if len(t_plot) == 0:
            return

        x_disp, y_disp, z_disp = x.copy(), y.copy(), z.copy()

        if "50Hz" in filter_mode:
            w = 20  # ~50 Hz smoothing window at 1 kS/s
            if len(x_disp) >= w:
                x_disp = np.convolve(x_disp, np.ones(w)/w, mode='same')
                y_disp = np.convolve(y_disp, np.ones(w)/w, mode='same')
                z_disp = np.convolve(z_disp, np.ones(w)/w, mode='same')
        elif "10Hz" in filter_mode:
            w = 100  # ~10 Hz smoothing window at 1 kS/s
            if len(x_disp) >= w:
                x_disp = np.convolve(x_disp, np.ones(w)/w, mode='same')
                y_disp = np.convolve(y_disp, np.ones(w)/w, mode='same')
                z_disp = np.convolve(z_disp, np.ones(w)/w, mode='same')

        self.curve_x.setData(t_plot, x_disp)
        self.curve_y.setData(t_plot, y_disp)
        self.curve_z.setData(t_plot, z_disp)
