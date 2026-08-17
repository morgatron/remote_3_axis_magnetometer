"""
UI Widgets for Desktop App.
"""
from .time_series_plot import TimeSeriesPlot
from .psd_plot import PsdPlot
from .stats_panel import StatsPanel
from .acquisition_sidebar import AcquisitionSidebar
from .provision_dialog import ProvisionDialog

__all__ = [
    "TimeSeriesPlot",
    "PsdPlot",
    "StatsPanel",
    "AcquisitionSidebar",
    "ProvisionDialog"
]
