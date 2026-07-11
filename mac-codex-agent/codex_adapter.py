"""Collect the small Codex status snapshot rendered by the StopWatch."""

from __future__ import annotations

import datetime
import json
import os
import pathlib
import re
import sqlite3
import ssl
import subprocess
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any, Callable, Dict, Optional


CODEX_USAGE_URL = "https://chatgpt.com/backend-api/wham/usage"
HTTP_TIMEOUT_SECONDS = 8
QUOTA_CACHE_SECONDS = 60
ACTIVITY_POLL_SECONDS = 5
CODEX_APP_NAMES = {"Codex"}


@dataclass(frozen=True)
class CodexPaths:
    config: pathlib.Path
    auth: pathlib.Path
    logs: pathlib.Path
    usage: pathlib.Path


DEFAULT_PATHS = CodexPaths(
    config=pathlib.Path.home() / ".codex" / "config.toml",
    auth=pathlib.Path.home() / ".codex" / "auth.json",
    logs=pathlib.Path.home() / ".codex" / "logs_2.sqlite",
    usage=pathlib.Path.home() / ".codex" / ".stopwatch_usage.json",
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
    ):
        self.paths = paths
        self._now = now
        self._quota_fetcher = quota_fetcher or self._fetch_quota
        self._quota_cache: Dict[str, Any] = {}
        self._quota_cache_time = 0.0

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
        return self._quota_cache

    def _load_usage(self) -> Dict[str, Any]:
        try:
            payload = json.loads(self.paths.usage.read_text(encoding="utf-8"))
            return payload if isinstance(payload, dict) else {}
        except (OSError, json.JSONDecodeError):
            return {}

    def _save_usage(self, usage: Dict[str, Any]) -> None:
        self.paths.usage.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.paths.usage.with_suffix(self.paths.usage.suffix + ".tmp")
        temporary.write_text(json.dumps(usage, indent=2), encoding="utf-8")
        os.replace(str(temporary), str(self.paths.usage))

    def record_frontmost_app(self, app_name: str) -> None:
        now = self._now()
        usage = self._load_usage()
        days = usage.setdefault("days", {})
        weeks = usage.setdefault("weeks", {})
        if app_name in CODEX_APP_NAMES:
            day = _day_key(now)
            week = _week_key(now)
            days[day] = days.get(day, 0) + ACTIVITY_POLL_SECONDS
            weeks[week] = weeks.get(week, 0) + ACTIVITY_POLL_SECONDS
        usage["last_seen_ts"] = now
        usage["last_front"] = app_name
        self._save_usage(usage)

    def get_state(self) -> Dict[str, Any]:
        now = self._now()
        status = self._read_config_state()
        status.update(self._read_live_state())
        quota = self._get_quota()
        usage = self._load_usage()
        today_seconds = (usage.get("days") or {}).get(_day_key(now), 0)
        week_seconds = (usage.get("weeks") or {}).get(_week_key(now), 0)
        five_hour_used = quota.get("five_hour_used_pct")
        week_used = quota.get("week_used_pct")
        return {
            "codex_model": status.get("model") or "",
            "codex_effort": status.get("effort") or "",
            "codex_5h_left_pct": _remaining_percent(five_hour_used),
            "codex_week_left_pct": _remaining_percent(week_used),
            "codex_today_min": int(today_seconds // 60),
            "codex_week_min": int(week_seconds // 60),
            "codex_status_ok": bool(status.get("model")),
            "codex_plan": quota.get("plan") or "",
        }


def _integer_percent(value: Any) -> Optional[int]:
    if not isinstance(value, (int, float)):
        return None
    return max(0, min(100, int(value)))


def _remaining_percent(value: Any) -> Optional[int]:
    used = _integer_percent(value)
    return None if used is None else 100 - used


def _day_key(timestamp: float) -> str:
    return datetime.datetime.fromtimestamp(timestamp).strftime("%Y-%m-%d")


def _week_key(timestamp: float) -> str:
    current = datetime.datetime.fromtimestamp(timestamp)
    monday = current - datetime.timedelta(days=current.weekday())
    return monday.strftime("%Y-%m-%d")


def frontmost_app() -> str:
    try:
        output = subprocess.check_output(
            [
                "osascript",
                "-e",
                'tell application "System Events" to name of first '
                "application process whose frontmost is true",
            ],
            stderr=subprocess.DEVNULL,
            timeout=2,
        )
        return output.decode("utf-8").strip()
    except (OSError, subprocess.SubprocessError):
        return ""


def activity_loop(collector: CodexStateCollector) -> None:
    while True:
        time.sleep(ACTIVITY_POLL_SECONDS)
        collector.record_frontmost_app(frontmost_app())
