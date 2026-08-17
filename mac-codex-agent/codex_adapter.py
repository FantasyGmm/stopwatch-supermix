"""Collect the small Codex status snapshot rendered by the StopWatch."""

from __future__ import annotations

import datetime
import json
import os
import pathlib
import re
import sqlite3
import ssl
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any, Callable, Dict, Iterable, Optional, Tuple

from task_alerts import TaskAlertSnapshot
from task_routes import route_id_for_thread


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
    state_db: pathlib.Path = pathlib.Path.home() / ".codex" / "state_5.sqlite"
    model_selection_cache: pathlib.Path = (
        pathlib.Path.home() / ".codex" / ".stopwatch_model_selection.json"
    )


DEFAULT_PATHS = CodexPaths(
    config=pathlib.Path.home() / ".codex" / "config.toml",
    auth=pathlib.Path.home() / ".codex" / "auth.json",
    logs=pathlib.Path.home() / ".codex" / "logs_2.sqlite",
    sessions=pathlib.Path.home() / ".codex" / "sessions",
)


def _window_minutes(window: Dict[str, Any]) -> Optional[int]:
    minutes = window.get("window_minutes")
    if isinstance(minutes, (int, float)):
        return int(minutes)
    seconds = window.get("limit_window_seconds")
    if isinstance(seconds, (int, float)):
        return int(seconds) // 60
    return None


def _quota_from_windows(
    primary: Dict[str, Any],
    secondary: Dict[str, Any],
    plan: Any,
) -> Dict[str, Any]:
    quota: Dict[str, Any] = {"plan": plan or ""}
    unclassified = []
    for position, window in (("primary", primary), ("secondary", secondary)):
        used = _integer_percent(window.get("used_percent"))
        if used is None:
            continue
        duration = _window_minutes(window)
        if duration == 300:
            quota["five_hour_used_pct"] = used
        elif duration == 10080:
            quota["week_used_pct"] = used
        else:
            unclassified.append((position, used))
    for position, used in unclassified:
        key = "five_hour_used_pct" if position == "primary" else "week_used_pct"
        quota.setdefault(key, used)
    return quota


def _default_ssl_context():
    try:
        import certifi

        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()


def _majority_running_pair(
    candidates: Iterable[Tuple[Any, Any, Any]],
) -> Optional[Tuple[str, str]]:
    counts: Dict[Tuple[str, str], int] = {}
    latest: Dict[Tuple[str, str], str] = {}
    for model, effort, started_at in candidates:
        model_text = model.strip() if isinstance(model, str) else ""
        effort_text = effort.strip().lower() if isinstance(effort, str) else ""
        timestamp = started_at if isinstance(started_at, str) else ""
        if not model_text or not effort_text:
            continue
        pair = (model_text, effort_text)
        counts[pair] = counts.get(pair, 0) + 1
        latest[pair] = max(latest.get(pair, ""), timestamp)
    if not counts:
        return None
    return max(counts, key=lambda pair: (counts[pair], latest[pair], pair))


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
        self._remembered_model_pair = self._load_remembered_model_pair()

    def _load_remembered_model_pair(self) -> Optional[Tuple[str, str]]:
        try:
            payload = json.loads(
                self.paths.model_selection_cache.read_text(encoding="utf-8")
            )
        except (OSError, json.JSONDecodeError):
            return None
        if not isinstance(payload, dict):
            return None
        model = payload.get("model")
        effort = payload.get("effort")
        if not isinstance(model, str) or not isinstance(effort, str):
            return None
        pair = (model.strip(), effort.strip().lower())
        return pair if all(pair) else None

    def _save_remembered_model_pair(self, pair: Tuple[str, str]) -> None:
        path = self.paths.model_selection_cache
        temporary = path.with_name(path.name + ".tmp")
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            temporary.write_text(
                json.dumps(
                    {"model": pair[0], "effort": pair[1]},
                    ensure_ascii=False,
                    separators=(",", ":"),
                ),
                encoding="utf-8",
            )
            os.replace(temporary, path)
        except OSError:
            try:
                temporary.unlink()
            except OSError:
                pass

    def _read_running_model_pair(
        self, alerts: TaskAlertSnapshot
    ) -> Optional[Tuple[str, str]]:
        running = {
            alert.thread_id: alert.progress_at
            for alert in alerts.alerts
            if alert.state == "running" and alert.thread_id
        }
        if not running or not self.paths.state_db.exists():
            return None
        placeholders = ",".join("?" for _ in running)
        try:
            connection = sqlite3.connect(
                "file:{}?mode=ro".format(self.paths.state_db),
                uri=True,
                timeout=1.0,
            )
            try:
                rows = connection.execute(
                    "SELECT id, model, reasoning_effort FROM threads "
                    "WHERE id IN ({})".format(placeholders),
                    tuple(running),
                ).fetchall()
            finally:
                connection.close()
        except (OSError, sqlite3.Error):
            return None
        return _majority_running_pair(
            (model, effort, running.get(str(thread_id), ""))
            for thread_id, model, effort in rows
        )

    def _apply_running_model_selection(
        self, status: Dict[str, Any], alerts: TaskAlertSnapshot
    ) -> None:
        winner = self._read_running_model_pair(alerts)
        if winner is not None and winner != self._remembered_model_pair:
            self._remembered_model_pair = winner
            self._save_remembered_model_pair(winner)
        if self._remembered_model_pair is not None:
            status["model"], status["effort"] = self._remembered_model_pair

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
                if limits.get("limit_name"):
                    continue
                primary = limits.get("primary") or {}
                secondary = limits.get("secondary") or {}
                quota = _quota_from_windows(
                    primary,
                    secondary,
                    limits.get("plan_type"),
                )
                if not any(
                    key in quota
                    for key in ("five_hour_used_pct", "week_used_pct")
                ):
                    continue
                reset_values = [
                    window.get("resets_at")
                    for window in (primary, secondary)
                    if isinstance(window.get("resets_at"), (int, float))
                ]
                latest_reset = int(max(reset_values)) if reset_values else -1
                key = (str(event.get("timestamp") or ""), latest_reset)
                if key <= best_key:
                    continue
                best_key = key
                best = quota
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
        return _quota_from_windows(
            primary,
            secondary,
            payload.get("plan_type"),
        )

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
        self._apply_running_model_selection(status, alerts)
        watch_notification_ids = tuple(
            alert.id
            for alert in alerts.alerts
            if alert.state == "pending" and not alert.watch_notified
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
            "_codex_watch_notification_ids": watch_notification_ids,
            "codex_alerts": [
                {
                    "route": route_id_for_thread(alert.thread_id),
                    "thread_id": alert.thread_id,
                    "project": alert.project,
                    "title": alert.title,
                    "time": _format_alert_time(
                        alert.progress_at, self._local_timezone
                    ),
                    "state": alert.state,
                    "outcome": alert.outcome,
                    "turn_id": alert.turn_id,
                    "last_reply": alert.last_reply,
                }
                for alert in alerts.alerts
            ],
        }

    def pending_watch_notification_ids(self):
        if self._alert_tracker is None:
            return ()
        return self._alert_tracker.pending_watch_notification_ids()

    def wait_for_change(self, timeout: float) -> bool:
        if self._alert_tracker is None:
            time.sleep(max(0.0, timeout))
            return False
        method = getattr(self._alert_tracker, "wait_for_change", None)
        if method is None:
            time.sleep(max(0.0, timeout))
            return False
        return bool(method(timeout))

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
