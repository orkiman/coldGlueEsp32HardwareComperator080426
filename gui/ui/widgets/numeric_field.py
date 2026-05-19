"""Labelled numeric field with units, debounced live-publish."""
from __future__ import annotations

from typing import Callable

from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtWidgets import (QDoubleSpinBox, QHBoxLayout, QLabel, QSpinBox,
                               QWidget)


class NumericField(QWidget):
    """A labeled spinbox that emits `committed(value)` ~300ms after the user
    stops editing — so live publish doesn't spam the link."""

    committed = Signal(float)

    def __init__(self,
                 label: str,
                 unit: str = "",
                 minimum: float = 0.0,
                 maximum: float = 9999.0,
                 step: float = 0.1,
                 decimals: int = 2,
                 integer: bool = False) -> None:
        super().__init__()
        self._spin: QDoubleSpinBox | QSpinBox
        if integer:
            self._spin = QSpinBox()
            self._spin.setRange(int(minimum), int(maximum))
            self._spin.setSingleStep(max(1, int(step)))
        else:
            self._spin = QDoubleSpinBox()
            self._spin.setRange(minimum, maximum)
            self._spin.setSingleStep(step)
            self._spin.setDecimals(decimals)
        self._spin.setMinimumWidth(140)
        self._spin.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        # Force LTR layout on the spinbox itself so the arrows stay on the
        # right of the digits and the value reads naturally regardless of
        # the surrounding RTL layout.
        self._spin.setLayoutDirection(Qt.LeftToRight)

        self._debounce = QTimer(self)
        self._debounce.setInterval(300)
        self._debounce.setSingleShot(True)
        self._debounce.timeout.connect(self._emit)
        self._spin.valueChanged.connect(lambda *_: self._debounce.start())

        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(QLabel(label))
        lay.addStretch(1)
        lay.addWidget(self._spin)
        if unit:
            lay.addWidget(QLabel(unit))

    def setValue(self, v: float) -> None:  # noqa: N802
        self._spin.blockSignals(True)
        self._spin.setValue(int(v) if isinstance(self._spin, QSpinBox) else float(v))
        self._spin.blockSignals(False)

    def value(self) -> float:
        return float(self._spin.value())

    def bind(self, on_commit: Callable[[float], None]) -> None:
        self.committed.connect(on_commit)

    def _emit(self) -> None:
        self.committed.emit(float(self._spin.value()))
