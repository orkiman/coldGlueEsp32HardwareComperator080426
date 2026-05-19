"""Abstract NDJSON transport. Real (serial) and mock subclasses share this."""
from __future__ import annotations

from typing import Any

from PySide6.QtCore import QObject, Signal


class LinkBase(QObject):
    """Common interface for any NDJSON transport.

    Signals:
        event_received(dict): emitted on every event line from the device.
        connection_changed(bool, str): (connected, human-readable reason).
    """

    event_received     = Signal(dict)
    connection_changed = Signal(bool, str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._connected = False

    # ---- to be implemented by subclasses ------------------------------------
    def open(self, *args: Any, **kwargs: Any) -> None:  # noqa: A003
        raise NotImplementedError

    def close(self) -> None:
        raise NotImplementedError

    def send(self, payload: dict[str, Any]) -> None:
        raise NotImplementedError

    # ---- shared helpers -----------------------------------------------------
    @property
    def connected(self) -> bool:
        return self._connected

    def _set_connected(self, ok: bool, reason: str = "") -> None:
        if ok != self._connected:
            self._connected = ok
            self.connection_changed.emit(ok, reason)
