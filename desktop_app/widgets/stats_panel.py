from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel

class StatsPanel(QFrame):
    """
    Channel statistics panel displaying real-time X, Y, Z means.
    """
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFrameShape(QFrame.StyledPanel)
        self.setStyleSheet("""
            QFrame {
                background-color: rgba(128, 128, 128, 0.1);
                border: 1px solid rgba(128, 128, 128, 0.2);
                border-radius: 4px;
            }
        """)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(15, 6, 15, 6)

        title = QLabel("Channel Means:")
        title.setStyleSheet("font-weight: bold;")
        layout.addWidget(title)

        layout.addSpacing(20)
        self.mean_x_label = QLabel("X: 0.00")
        self.mean_x_label.setStyleSheet("color: #ef5350; font-weight: bold; font-size: 13px;")
        layout.addWidget(self.mean_x_label)

        layout.addSpacing(20)
        self.mean_y_label = QLabel("Y: 0.00")
        self.mean_y_label.setStyleSheet("color: #66bb6a; font-weight: bold; font-size: 13px;")
        layout.addWidget(self.mean_y_label)

        layout.addSpacing(20)
        self.mean_z_label = QLabel("Z: 0.00")
        self.mean_z_label.setStyleSheet("color: #42a5f5; font-weight: bold; font-size: 13px;")
        layout.addWidget(self.mean_z_label)

        layout.addStretch()

    def update_means(self, mean_x: float, mean_y: float, mean_z: float, unit_str: str = "nT"):
        """Updates mean value labels."""
        self.mean_x_label.setText(f"X: {mean_x:.1f} {unit_str}")
        self.mean_y_label.setText(f"Y: {mean_y:.1f} {unit_str}")
        self.mean_z_label.setText(f"Z: {mean_z:.1f} {unit_str}")
