"""In-process fake firmware for UI development without hardware.

Accepts the same NDJSON commands and emits plausible status / ack / event
traffic so the GUI can be developed and demoed end-to-end.
"""
from __future__ import annotations

import random
from typing import Any

from PySide6.QtCore import QTimer

from .link_base import LinkBase


class MockLink(LinkBase):
    def __init__(self) -> None:
        super().__init__()
        self._active = False
        self._fault = False
        self._sheet_count = 0
        self._speed = 0.0
        self._target_speed = 480.0  # mm/s
        self._status_timer = QTimer(self)
        self._status_timer.setInterval(200)
        self._status_timer.timeout.connect(self._emit_status)
        self._sheet_timer = QTimer(self)
        self._sheet_timer.setInterval(700)
        self._sheet_timer.timeout.connect(self._emit_sheet)

    # ---- LinkBase API -------------------------------------------------------
    def open(self, *_a: Any, **_kw: Any) -> None:  # noqa: A003
        self.start()

    def close(self) -> None:
        self._status_timer.stop()
        self._sheet_timer.stop()
        self._set_connected(False, "mock closed")

    def send(self, payload: dict[str, Any]) -> None:
        cmd = payload.get("cmd", "")
        if cmd == "ping":
            self._ack("ping")
        elif cmd == "set_active":
            self._active = bool(payload.get("active", False))
            if self._active:
                self._fault = False
                self._sheet_timer.start()
            else:
                self._sheet_timer.stop()
                self._speed = 0.0
            self._ack("set_active")
        elif cmd == "set_config":
            self._ack("set_config")
        elif cmd == "set_pattern":
            self._ack("set_pattern")
        elif cmd == "test_open":
            self._ack("test_open")
        elif cmd == "test_close":
            self._ack("test_close")
        elif cmd == "calib_arm":
            self._ack("calib_arm")
            # Simulate a calibration result after a moment.
            QTimer.singleShot(800, lambda: self.event_received.emit({
                "event": "calib_result",
                "pulses_per_mm": 12.34 + random.uniform(-0.05, 0.05),
            }))
        elif cmd == "sw_trigger":
            self._ack("sw_trigger")
        else:
            self.event_received.emit({"event": "error",
                                      "cmd": cmd, "reason": "unknown_cmd"})

    # ---- lifecycle ----------------------------------------------------------
    def start(self) -> None:
        self._set_connected(True, "mock")
        self._status_timer.start()

    # ---- internals ----------------------------------------------------------
    def _ack(self, cmd: str) -> None:
        self.event_received.emit({"event": "ack", "cmd": cmd})

    def _emit_status(self) -> None:
        if self._active and not self._fault:
            # Wobble around target.
            self._speed += (self._target_speed - self._speed) * 0.2
            self._speed += random.uniform(-5, 5)
        else:
            self._speed *= 0.5
        self.event_received.emit({
            "event": "status",
            "active": self._active,
            "fault": self._fault,
            "speed_mm_s": round(self._speed, 1),
            "sheet_count": self._sheet_count,
            "queue_depth": 0,
        })

    def _emit_sheet(self) -> None:
        if not (self._active and not self._fault):
            return
        self._sheet_count += 1
        self.event_received.emit({
            "event": "pattern_event",
            "kind": "sheet_detected",
            "sheet_count": self._sheet_count,
        })
