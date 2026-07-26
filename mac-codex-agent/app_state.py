"""Thread-safe value types and presentation rules for the menu bar agent."""

from __future__ import annotations

import dataclasses
import enum
import time
from typing import Any, Callable, Mapping, Optional, Protocol, Tuple

from voice_session import VoiceState


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
class TaskAlertView:
    project: str
    title: str
    time: str
    state: str
    outcome: str


@dataclasses.dataclass(frozen=True)
class CodexSnapshot:
    model: str = ""
    effort: str = ""
    five_hour_left_pct: Optional[int] = None
    week_left_pct: Optional[int] = None
    alert_pending: int = 0
    alert_running: int = 0
    alert_completed: int = 0
    alert_revision: int = 0
    alerts: Tuple[TaskAlertView, ...] = ()
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
            alert_pending=_normalized_integer(status.get("codex_alert_pending")) or 0,
            alert_running=_normalized_integer(status.get("codex_alert_running")) or 0,
            alert_completed=(
                _normalized_integer(status.get("codex_alert_completed")) or 0
            ),
            alert_revision=_normalized_integer(status.get("codex_alert_revision")) or 0,
            alerts=_alert_views(status.get("codex_alerts")),
            updated_at=now(),
        )


@dataclasses.dataclass(frozen=True)
class AgentSnapshot:
    state: ConnectionState
    connection_enabled: bool
    detail: str = ""
    voice_state: VoiceState = VoiceState.ERROR
    voice_detail: str = ""
    codex: CodexSnapshot = dataclasses.field(default_factory=CodexSnapshot)
    changed_at: float = dataclasses.field(default_factory=time.time)


@dataclasses.dataclass(frozen=True)
class MenuPresentation:
    model_text: str
    effort_text: str
    five_hour_text: str
    seven_day_text: str
    alert_summary_text: str
    alerts: Tuple[TaskAlertView, ...]
    alert_revision: int
    agent_text: str
    bluetooth_text: str
    device_text: str
    voice_state: VoiceState
    voice_text: str
    voice_detail: str
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

_VOICE_STATE_LABELS = {
    VoiceState.DRIVER_REQUIRED: "Driver Required",
    VoiceState.RESTART_REQUIRED: "Restart Required",
    VoiceState.ACCESSIBILITY_REQUIRED: "Accessibility Required",
    VoiceState.TYPELESS_NOT_RUNNING: "Typeless Not Running",
    VoiceState.READY: "Ready",
    VoiceState.RECORDING: "Recording",
    VoiceState.ERROR: "Error",
}


def present(snapshot: AgentSnapshot) -> MenuPresentation:
    codex = snapshot.codex
    voice_text = "Voice Input: {}".format(
        _VOICE_STATE_LABELS[snapshot.voice_state]
    )
    if snapshot.voice_detail:
        voice_text = "{} - {}".format(voice_text, snapshot.voice_detail)
    return MenuPresentation(
        model_text="Model: {}".format(codex.model or "--"),
        effort_text="Effort: {}".format(codex.effort.title() if codex.effort else "--"),
        five_hour_text="5H Remaining: {}".format(
            _format_number(codex.five_hour_left_pct, "%")
        ),
        seven_day_text="7D Remaining: {}".format(
            _format_number(codex.week_left_pct, "%")
        ),
        alert_summary_text=(
            "Task Alerts · {} Pending / {} Running / {} Completed".format(
                codex.alert_pending, codex.alert_running, codex.alert_completed
            )
        ),
        alerts=codex.alerts,
        alert_revision=codex.alert_revision,
        agent_text="Agent: Running",
        bluetooth_text="Bluetooth: {}".format(_STATE_LABELS[snapshot.state]),
        device_text="Device: {}".format(DEVICE_NAME),
        voice_state=snapshot.voice_state,
        voice_text=voice_text,
        voice_detail=snapshot.voice_detail,
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


def _alert_views(value: Any) -> Tuple[TaskAlertView, ...]:
    if not isinstance(value, list):
        return ()

    alerts = []
    for entry in value:
        if not isinstance(entry, Mapping):
            continue
        project = _text_value(entry.get("project"))
        title = _text_value(entry.get("title"))
        alert_time = _text_value(entry.get("time"))
        state = _text_value(entry.get("state")).lower()
        outcome = _text_value(entry.get("outcome")).lower()
        if (
            project
            and title
            and alert_time
            and (state, outcome)
            in (
                ("pending", "waiting"),
                ("running", "running"),
                ("completed", "completed"),
            )
        ):
            alerts.append(TaskAlertView(project, title, alert_time, state, outcome))
    return tuple(alerts)


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
