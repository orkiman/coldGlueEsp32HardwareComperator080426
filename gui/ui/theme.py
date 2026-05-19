"""Dark industrial theme loader."""
from __future__ import annotations

from pathlib import Path

from PySide6.QtWidgets import QApplication


def apply_dark_theme(app: QApplication) -> None:
    qss_path = Path(__file__).with_name("styles.qss")
    if qss_path.is_file():
        app.setStyleSheet(qss_path.read_text(encoding="utf-8"))
