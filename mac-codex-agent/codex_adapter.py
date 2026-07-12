"""Collect the small Codex status snapshot rendered by the StopWatch."""

from __future__ import annotations

import datetime
import json
import pathlib
import re
import sqlite3
import ssl
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any, Callable, Dict, Optional

from task_alerts import TaskAlertSnapshot


CODEX_USAGE_URL = "https://chatgpt.com/backend-api/wham/usage"
HTTP_TIMEOUT_SECONDS = 8
QUOTA_CACHE_SECONDS = 60
QUOTA_STALE_SECONDS = 120
SESSION_FILE_LIMIT = 32
SESSION_TAIL_BYTES = 256 * 1024


@dataclass(frozen=True)
class CodexPaths:
    config: pathlib.Path
    auth: pathlib.Path
    logs: pathlib.Path
    sessions: pathlib.Path = pathlib.Path.home() / ".codex" / "sessions"


DEFAULT_PATHS = CodexPaths(
    config=pathlib.Path.home() / ".codex" / "config.toml",
    auth=pathlib.Path.home() / ".codex" / "auth.json",
    logs=pathlib.Path.home() / ".codex" / "logs_2.sqlite",
    sessions=pathlib.Path.home() / ".codex" / "sessions",
)


def _default_ssl_context():
    try:
        import certifi

        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()


class CodexStateCollector:
    def __init__(
        self,
        paths: CodexPaths = DEFAULT_PATHS,
        now: Callable[[], float] = time.time,
        quota_fetcher: Optional[Callable[[], Dict[str, Any]]] = None,
        alert_tracker=None,
        local_timezone=None,
    ):
        self.paths = paths
        self._now = now
        self._quota_fetcher = quota_fetcher or self._fetch_quota
        self._quota_cache: Dict[str, Any] = {}
        self._quota_cache_time = 0.0
        self._alert_tracker = alert_tracker
        self._local_timezone = local_timezone or datetime.datetime.now().astimezone().tzinfo

    def _read_session_quota(self) -> Dict[str, Any]:
        try:
            session_files = sorted(
                self.paths.sessions.rglob("*.jsonl"),
                key=lambda path: path.stat().st_mtime,
                reverse=True,
            )[:SESSION_FILE_LIMIT]
        except OSError:
            return {}

        best_key = ("", -1)
        best: Dict[str, Any] = {}
        for path in session_files:
            try:
                with path.open("rb") as handle:
                    handle.seek(0, 2)
                    size = handle.tell()
                    offset = max(0, size - SESSION_TAIL_BYTES)
                    handle.seek(offset)
                    content = handle.read().decode("utf-8", errors="ignore")
                lines = content.splitlines()
                if offset:
                    lines = lines[1:]
            except OSError:
                continue
            for line in lines:
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if event.get("type") != "event_msg":
                    continue
                payload = event.get("payload") or {}
                if payload.get("type") != "token_count":
                    continue
                limits = payload.get("rate_limits") or {}
                primary = limits.get("primary") or {}
                secondary = limits.get("secondary") or {}
                used = _integer_percent(primary.get("used_percent"))
                reset_at = primary.get("resets_at")
                if used is None or not isinstance(reset_at, (int, float)):
                    continue
                key = (str(event.get("timestamp") or ""), int(reset_at))
                if key <= best_key:
                    continue
                best_key = key
                best = {
                    "five_hour_used_pct": used,
                    "week_used_pct": _integer_percent(secondary.get("used_percent")),
                    "plan": limits.get("plan_type") or "",
                }
        return best

    def _read_config_state(self) -> Dict[str, Any]:
        try:
            text = self.paths.config.read_text(encoding="utf-8")
        except OSError:
            return {}

        state: Dict[str, Any] = {}
        model = re.search(r'^\s*model\s*=\s*"([^"]+)"', text, re.MULTILINE)
        if model:
            state["model"] = model.group(1).strip()
        effort = re.search(
            r'^\s*model_reasoning_effort\s*=\s*"([^"]+)"',
            text,
            re.MULTILINE,
        )
        if effort:
            state["effort"] = effort.group(1).strip().lower()
        return state

    def _read_live_state(self) -> Dict[str, Any]:
        if not self.paths.logs.exists():
            return {}
        try:
            connection = sqlite3.connect(
                "file:{}?mode=ro".format(self.paths.logs), uri=True, timeout=1.0
            )
            try:
                row = connection.execute(
                    "SELECT feedback_log_body FROM logs "
                    "WHERE feedback_log_body LIKE "
                    "'%codex.turn.reasoning_effort=%' "
                    "ORDER BY id DESC LIMIT 1"
                ).fetchone()
            finally:
                connection.close()
        except sqlite3.Error:
            return {}
        if not row or not row[0]:
            return {}

        state: Dict[str, Any] = {}
        body = str(row[0])
        effort = re.search(r"codex\.turn\.reasoning_effort=(\w+)", body)
        if effort:
            state["effort"] = effort.group(1).lower()
        model = re.search(r"\bmodel=([\w.\-]+)", body)
        if model:
            state["model"] = model.group(1)
        return state

    def _fetch_quota(self) -> Dict[str, Any]:
        try:
            auth = json.loads(self.paths.auth.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return {}
        token = (auth.get("tokens") or {}).get("access_token")
        if not token:
            return {}

        request = urllib.request.Request(
            CODEX_USAGE_URL,
            headers={"Authorization": "Bearer {}".format(token)},
            method="GET",
        )
        try:
            with urllib.request.urlopen(
                request,
                timeout=HTTP_TIMEOUT_SECONDS,
                context=_default_ssl_context(),
            ) as response:
                if response.status != 200:
                    return {}
                payload = json.loads(response.read())
        except (OSError, urllib.error.URLError, json.JSONDecodeError):
            return {}
        if not isinstance(payload, dict):
            return {}

        limits = payload.get("rate_limit") or {}
        primary = limits.get("primary_window") or {}
        secondary = limits.get("secondary_window") or {}
        return {
            "five_hour_used_pct": _integer_percent(primary.get("used_percent")),
            "week_used_pct": _integer_percent(secondary.get("used_percent")),
            "plan": payload.get("plan_type") or "",
        }

    def _get_quota(self) -> Dict[str, Any]:
        session_quota = self._read_session_quota()
        if session_quota:
            return session_quota
        now = self._now()
        if self._quota_cache and now - self._quota_cache_time < QUOTA_CACHE_SECONDS:
            return self._quota_cache
        try:
            fresh = self._quota_fetcher()
        except Exception:
            fresh = {}
        if fresh:
            self._quota_cache = fresh
            self._quota_cache_time = now
        elif now - self._quota_cache_time >= QUOTA_STALE_SECONDS:
            self._quota_cache = {}
        return self._quota_cache

    def get_state(self) -> Dict[str, Any]:
        status = self._read_config_state()
        status.update(self._read_live_state())
        quota = self._get_quota()
        alerts = (
            self._alert_tracker.refresh()
            if self._alert_tracker is not None
            else TaskAlertSnapshot()
        )
        five_hour_used = quota.get("five_hour_used_pct")
        week_used = quota.get("week_used_pct")
        return {
            "codex_model": status.get("model") or "",
            "codex_effort": status.get("effort") or "",
            "codex_5h_left_pct": _remaining_percent(five_hour_used),
            "codex_week_left_pct": _remaining_percent(week_used),
            "codex_status_ok": bool(status.get("model")),
            "codex_plan": quota.get("plan") or "",
            "codex_alert_pending": alerts.pending_count,
            "codex_alert_running": alerts.running_count,
            "codex_alert_completed": alerts.completed_count,
            "codex_alert_revision": alerts.revision,
            "codex_alerts": [
                {
                    "project": alert.project,
                    "title": alert.title,
                    "time": _format_alert_time(
                        alert.progress_at, self._local_timezone
                    ),
                    "state": alert.state,
                    "outcome": alert.outcome,
                }
                for alert in alerts.alerts
            ],
        }

    def pending_watch_notification_ids(self):
        if self._alert_tracker is None:
            return ()
        return self._alert_tracker.pending_watch_notification_ids()

    def mark_watch_notified(self, alert_ids) -> None:
        if self._alert_tracker is not None:
            self._alert_tracker.mark_watch_notified(alert_ids)


def _integer_percent(value: Any) -> Optional[int]:
    if not isinstance(value, (int, float)):
        return None
    return max(0, min(100, int(value)))


def _remaining_percent(value: Any) -> Optional[int]:
    used = _integer_percent(value)
    return None if used is None else 100 - used


def _format_alert_time(value: str, local_timezone) -> str:
    try:
        timestamp = datetime.datetime.fromisoformat(value.replace("Z", "+00:00"))
        if timestamp.tzinfo is None:
            timestamp = timestamp.replace(tzinfo=datetime.timezone.utc)
        return timestamp.astimezone(local_timezone).strftime("%H:%M")
    except (AttributeError, TypeError, ValueError):
        return "--:--"
