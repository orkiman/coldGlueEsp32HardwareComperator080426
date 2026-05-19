"""Top bar: COM picker + connect/disconnect + status LED."""
from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QPainter
from PySide6.QtWidgets import (QComboBox, QHBoxLayout, QLabel, QPushButton,
                               QWidget)

from app.serial_link import SerialLink, list_ports
from app.state import AppState


class StatusLed(QWidget):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setFixedSize(18, 18)
        self._on = False

    def setOn(self, on: bool) -> None:  # noqa: N802 — Qt-style
        if on != self._on:
            self._on = on
            self.update()

    def paintEvent(self, _ev):  # noqa: N802
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        color = QColor("#22c55e") if self._on else QColor("#ef4444")
        p.setBrush(color)
        p.setPen(Qt.NoPen)
        p.drawEllipse(2, 2, 14, 14)


class ConnectionBar(QWidget):
    def __init__(self, state: AppState) -> None:
        super().__init__()
        self.state = state

        self.combo = QComboBox()
        self.combo.setMinimumWidth(260)
        self.btn   = QPushButton("התחבר")
        self.btn.clicked.connect(self._toggle)
        self.refresh = QPushButton("רענן")
        self.refresh.clicked.connect(self._populate)

        self.led = StatusLed()
        self.status_label = QLabel("מנותק")

        lay = QHBoxLayout(self)
        lay.setContentsMargins(12, 8, 12, 8)
        lay.addWidget(QLabel("יציאה:"))
        lay.addWidget(self.combo)
        lay.addWidget(self.refresh)
        lay.addWidget(self.btn)
        lay.addStretch(1)
        lay.addWidget(self.led)
        lay.addWidget(self.status_label)

        self._populate()
        state.connection_changed.connect(self._on_conn)

        # Disable connect controls if we're running on a mock link.
        if not isinstance(state.link, SerialLink):
            self.combo.setEnabled(False)
            self.btn.setEnabled(False)
            self.refresh.setEnabled(False)

    def _populate(self) -> None:
        self.combo.clear()
        for dev, desc in list_ports():
            self.combo.addItem(f"{dev} — {desc}", dev)
        if self.combo.count() == 0:
            self.combo.addItem("(לא נמצאו יציאות)", None)

    def _toggle(self) -> None:
        link = self.state.link
        if link.connected:
            link.close()
        else:
            port = self.combo.currentData()
            if port:
                link.open(port)

    def _on_conn(self, ok: bool, reason: str) -> None:
        self.led.setOn(ok)
        if ok:
            self.status_label.setText(f"מחובר ({reason})")
            self.btn.setText("נתק")
        else:
            self.status_label.setText("מנותק" if not reason else f"מנותק — {reason}")
            self.btn.setText("התחבר")
