"""Thread-safe value types and presentation rules for the menu bar agent."""

from __future__ import annotations

import dataclasses
import enum
import time
from typing import Any, Callable, Mapping, Optional, Protocol


DEVICE_NAME = "StopWatch-Codex"
CONNECTION_ENABLED_KEY = "connectionEnabled"


class ConnectionState(enum.Enum):
    DISABLED = "disabled"
    SCANNING = "scanning"
    WAITING = "waiting"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    DISCONNECTING = "disconnecting"
    UNAUTHORIZED = "unauthorized"
    UNAVAILABLE = "unavailable"
    ERROR = "error"


@dataclasses.dataclass(frozen=True)
class CodexSnapshot:
    model: str = ""
    effort: str = ""
    five_hour_left_pct: Optional[int] = None
    week_left_pct: Optional[int] = None
    today_minutes: Optional[int] = None
    week_minutes: Optional[int] = None
    updated_at: float = 0.0

    @classmethod
    def from_status(
        cls,
        status: Mapping[str, Any],
        now: Callable[[], float] = time.time,
    ) -> "CodexSnapshot":
        return cls(
            model=_text_value(status.get("codex_model")),
            effort=_text_value(status.get("codex_effort")),
            five_hour_left_pct=_normalized_integer(
                status.get("codex_5h_left_pct"), maximum=100
            ),
            week_left_pct=_normalized_integer(
                status.get("codex_week_left_pct"), maximum=100
            ),
            today_minutes=_normalized_integer(status.get("codex_today_min")),
            week_minutes=_normalized_integer(status.get("codex_week_min")),
            updated_at=now(),
        )


@dataclasses.dataclass(frozen=True)
class AgentSnapshot:
    state: ConnectionState
    connection_enabled: bool
    detail: str = ""
    codex: CodexSnapshot = dataclasses.field(default_factory=CodexSnapshot)
    changed_at: float = dataclasses.field(default_factory=time.time)


@dataclasses.dataclass(frozen=True)
class MenuPresentation:
    model_text: str
    effort_text: str
    five_hour_text: str
    seven_day_text: str
    today_text: str
    this_week_text: str
    agent_text: str
    bluetooth_text: str
    device_text: str
    connection_checked: bool
    reconnect_enabled: bool
    symbol_name: str


_STATE_LABELS = {
    ConnectionState.DISABLED: "Connection Off",
    ConnectionState.SCANNING: "Scanning",
    ConnectionState.WAITING: "Waiting for Watch",
    ConnectionState.CONNECTING: "Connecting",
    ConnectionState.CONNECTED: "Connected",
    ConnectionState.DISCONNECTING: "Disconnecting",
    ConnectionState.UNAUTHORIZED: "Permission Required",
    ConnectionState.UNAVAILABLE: "Unavailable",
    ConnectionState.ERROR: "Connection Error",
}


def present(snapshot: AgentSnapshot) -> MenuPresentation:
    codex = snapshot.codex
    return MenuPresentation(
        model_text="Model: {}".format(codex.model or "--"),
        effort_text="Effort: {}".format(codex.effort.title() if codex.effort else "--"),
        five_hour_text="5H Remaining: {}".format(
            _format_number(codex.five_hour_left_pct, "%")
        ),
        seven_day_text="7D Remaining: {}".format(
            _format_number(codex.week_left_pct, "%")
        ),
        today_text="Today: {}".format(
            _format_number(codex.today_minutes, " min")
        ),
        this_week_text="This Week: {}".format(
            _format_number(codex.week_minutes, " min")
        ),
        agent_text="Agent: Running",
        bluetooth_text="Bluetooth: {}".format(_STATE_LABELS[snapshot.state]),
        device_text="Device: {}".format(DEVICE_NAME),
        connection_checked=snapshot.connection_enabled,
        reconnect_enabled=snapshot.connection_enabled,
        symbol_name=(
            "bolt.horizontal.circle.fill"
            if snapshot.state is ConnectionState.CONNECTED
            else "bolt.horizontal.circle"
        ),
    )


def _text_value(value: Any) -> str:
    return value.strip() if isinstance(value, str) else ""


def _normalized_integer(value: Any, maximum: Optional[int] = None) -> Optional[int]:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    result = max(0, int(value))
    return min(maximum, result) if maximum is not None else result


def _format_number(value: Optional[int], suffix: str) -> str:
    return "--" if value is None else "{}{}".format(value, suffix)


class Preferences(Protocol):
    def connection_enabled(self) -> bool:
        ...

    def set_connection_enabled(self, value: bool) -> None:
        ...


class UserDefaultsPreferences:
    def __init__(self, defaults=None):
        if defaults is None:
            from Foundation import NSUserDefaults

            defaults = NSUserDefaults.standardUserDefaults()
        self._defaults = defaults
        self._defaults.registerDefaults_({CONNECTION_ENABLED_KEY: True})

    def connection_enabled(self) -> bool:
        return bool(self._defaults.boolForKey_(CONNECTION_ENABLED_KEY))

    def set_connection_enabled(self, value: bool) -> None:
        self._defaults.setBool_forKey_(bool(value), CONNECTION_ENABLED_KEY)


class MemoryPreferences:
    def __init__(self, connection_enabled: bool = True):
        self._connection_enabled = connection_enabled

    def connection_enabled(self) -> bool:
        return self._connection_enabled

    def set_connection_enabled(self, value: bool) -> None:
        self._connection_enabled = bool(value)
