"""Persist per-conversation Codex progress alerts from rollout events."""

from __future__ import annotations

import dataclasses
import datetime
import json
import logging
import os
import pathlib
import select
import shutil
import sqlite3
import subprocess
import threading
import time
from typing import Any, Callable, Dict, Iterable, List, Optional, Tuple


UTC = datetime.timezone.utc
DEFAULT_STATE_DB = pathlib.Path.home() / ".codex" / "state_5.sqlite"
DEFAULT_LEDGER_PATH = pathlib.Path.home() / ".codex" / ".stopwatch_alerts.json"
DEFAULT_CODEX_BINARIES = (
    pathlib.Path("/Applications/ChatGPT.app/Contents/Resources/codex"),
    pathlib.Path("/Applications/Codex.app/Contents/Resources/codex"),
)
LEDGER_VERSION = 2
RETENTION_DAYS = 7
MAX_DISCOVERED_THREADS = 50
THREAD_LIST_LIMIT = 200
THREAD_NAME_REFRESH_SECONDS = 30.0
APP_SERVER_TIMEOUT_SECONDS = 2.0
WAITING_PHRASES = (
    "请确认",
    "请回复",
    "请提供",
    "请选择",
    "需要你",
    "可以吗",
    "请重新",
    "告诉我",
    "等待你的",
    "等待回复",
    "等待确认",
    "需要进一步指令",
    "无法继续",
    "let me know",
    "please confirm",
    "please reply",
    "please provide",
    "please choose",
    "need your",
    "waiting for your",
    "cannot continue",
)


LOGGER = logging.getLogger(__name__)


def _codex_binary() -> Optional[str]:
    configured = os.environ.get("STOPWATCH_CODEX_BINARY")
    if configured and pathlib.Path(configured).is_file():
        return configured
    discovered = shutil.which("codex")
    if discovered:
        return discovered
    for candidate in DEFAULT_CODEX_BINARIES:
        if candidate.is_file():
            return str(candidate)
    return None


def _send_rpc(process, request_id: int, method: str, params: Dict[str, Any]) -> None:
    if process.stdin is None:
        raise OSError("Codex app server stdin is unavailable")
    process.stdin.write(
        json.dumps(
            {
                "jsonrpc": "2.0",
                "id": request_id,
                "method": method,
                "params": params,
            },
            separators=(",", ":"),
        )
        + "\n"
    )
    process.stdin.flush()


def _read_rpc_response(process, request_id: int, deadline: float) -> Dict[str, Any]:
    if process.stdout is None:
        return {}
    while process.poll() is None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return {}
        readable, _, _ = select.select([process.stdout], [], [], remaining)
        if not readable:
            return {}
        line = process.stdout.readline()
        if not line:
            return {}
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(message, dict) and message.get("id") == request_id:
            return message
    return {}


def load_codex_thread_names() -> Dict[str, str]:
    binary = _codex_binary()
    if binary is None:
        return {}
    process = None
    try:
        process = subprocess.Popen(
            [binary, "app-server", "--stdio"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )
        deadline = time.monotonic() + APP_SERVER_TIMEOUT_SECONDS
        _send_rpc(
            process,
            1,
            "initialize",
            {
                "clientInfo": {
                    "name": "stopwatch-codex-agent",
                    "title": "StopWatch Codex Agent",
                    "version": "2.0",
                },
                "capabilities": {},
            },
        )
        initialized = _read_rpc_response(process, 1, deadline)
        if not isinstance(initialized.get("result"), dict):
            return {}
        _send_rpc(
            process,
            2,
            "thread/list",
            {
                "limit": THREAD_LIST_LIMIT,
                "sortKey": "recency_at",
                "sortDirection": "desc",
            },
        )
        response = _read_rpc_response(process, 2, deadline)
        result = response.get("result")
        rows = result.get("data") if isinstance(result, dict) else None
        if not isinstance(rows, list):
            return {}
        names = {}
        for row in rows:
            if not isinstance(row, dict):
                continue
            thread_id = row.get("id")
            name = row.get("name")
            if isinstance(thread_id, str) and isinstance(name, str) and name.strip():
                names[thread_id] = name.strip()
        return names
    except (OSError, ValueError, subprocess.SubprocessError):
        LOGGER.debug("Unable to read Codex thread names", exc_info=True)
        return {}
    finally:
        if process is not None:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=0.5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=0.5)
            if process.stdin is not None:
                process.stdin.close()
            if process.stdout is not None:
                process.stdout.close()


def classify_outcome(message: Any) -> str:
    if not isinstance(message, str):
        return "completed"
    paragraphs = [part.strip() for part in message.split("\n\n") if part.strip()]
    if not paragraphs:
        return "completed"
    final = paragraphs[-1]
    lowered = final.casefold()
    if final.endswith(("?", "？")) or any(
        phrase in lowered for phrase in WAITING_PHRASES
    ):
        return "waiting"
    return "completed"


@dataclasses.dataclass(frozen=True)
class TaskAlert:
    id: str
    thread_id: str
    project: str
    title: str
    progress_at: str
    handled_at: Optional[str]
    state: str
    watch_notified: bool
    outcome: str


@dataclasses.dataclass(frozen=True)
class TaskAlertSnapshot:
    pending_count: int = 0
    running_count: int = 0
    completed_count: int = 0
    handled_count: int = 0
    revision: int = 0
    alerts: Tuple[TaskAlert, ...] = ()


@dataclasses.dataclass(frozen=True)
class _ThreadInfo:
    id: str
    cwd: str
    title: str
    rollout_path: pathlib.Path

    @property
    def project(self) -> str:
        name = pathlib.Path(self.cwd.rstrip("/")).name
        return name or self.cwd or "--"


def _utc_now() -> datetime.datetime:
    return datetime.datetime.now(tz=UTC)


def _iso(timestamp: datetime.datetime) -> str:
    if timestamp.tzinfo is None:
        timestamp = timestamp.replace(tzinfo=UTC)
    return timestamp.astimezone(UTC).isoformat().replace("+00:00", "Z")


def _parse_timestamp(value: Any) -> Optional[datetime.datetime]:
    if not isinstance(value, str) or not value:
        return None
    try:
        parsed = datetime.datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=UTC)
    return parsed.astimezone(UTC)


class TaskAlertTracker:
    def __init__(
        self,
        state_db: pathlib.Path = DEFAULT_STATE_DB,
        ledger_path: pathlib.Path = DEFAULT_LEDGER_PATH,
        now: Callable[[], datetime.datetime] = _utc_now,
        local_timezone=None,
        thread_name_loader: Callable[[], Dict[str, str]] = load_codex_thread_names,
        thread_name_refresh_seconds: float = THREAD_NAME_REFRESH_SECONDS,
    ):
        self.state_db = pathlib.Path(state_db)
        self.ledger_path = pathlib.Path(ledger_path)
        self._now = now
        self._local_timezone = local_timezone or datetime.datetime.now().astimezone().tzinfo
        self._thread_name_loader = thread_name_loader
        self._thread_name_refresh_seconds = max(0.0, thread_name_refresh_seconds)
        self._thread_names: Dict[str, str] = {}
        self._thread_names_loaded_at: Optional[float] = None
        self._lock = threading.RLock()
        self._state: Optional[Dict[str, Any]] = None
        self._needs_save = False

    def refresh(self) -> TaskAlertSnapshot:
        with self._lock:
            if self._state is None:
                loaded = self._load_state()
                if loaded is not None:
                    self._state = loaded
            threads = self._discover_threads()
            if self._state is None:
                self._state = self._initial_state(threads)
                self._save_state()
                return self._snapshot()

            changed = self._needs_save
            if self._scan_threads(threads):
                changed = True
            if self._sync_thread_metadata(threads):
                changed = True
            if self._prune_old_alerts():
                changed = True
            if changed:
                self._state["last_scan_at"] = _iso(self._now())
                self._save_state()
            return self._snapshot()

    def pending_watch_notification_ids(self) -> Tuple[str, ...]:
        with self._lock:
            self._ensure_loaded()
            return tuple(
                alert.id
                for alert in self._snapshot().alerts
                if alert.state == "pending" and not alert.watch_notified
            )

    def mark_watch_notified(self, alert_ids: Iterable[str]) -> None:
        target = set(alert_ids)
        if not target:
            return
        with self._lock:
            self._ensure_loaded()
            changed = False
            for item in self._state["alerts"]:
                if (
                    item.get("id") in target
                    and item.get("state") == "pending"
                    and not item.get("watch_notified", False)
                ):
                    item["watch_notified"] = True
                    changed = True
            if changed:
                self._save_state()

    def _ensure_loaded(self) -> None:
        if self._state is not None:
            return
        loaded = self._load_state()
        self._state = loaded or self._initial_state(self._discover_threads())
        if loaded is None:
            self._save_state()

    def _discover_threads(self) -> List[_ThreadInfo]:
        if not self.state_db.exists():
            return []
        try:
            connection = sqlite3.connect(
                "file:{}?mode=ro".format(self.state_db), uri=True, timeout=1.0
            )
            try:
                rows = connection.execute(
                    """
                    SELECT id, cwd, title, rollout_path
                      FROM threads
                     WHERE model_provider = 'openai'
                       AND (thread_source IS NULL OR thread_source != 'subagent')
                     ORDER BY updated_at_ms DESC
                     LIMIT ?
                    """,
                    (MAX_DISCOVERED_THREADS,),
                ).fetchall()

                known_ids = set((self._state or {}).get("cursors", {}))
                present_ids = {str(row[0]) for row in rows}
                missing = sorted(known_ids - present_ids)
                if missing:
                    placeholders = ",".join("?" for _ in missing)
                    rows.extend(
                        connection.execute(
                            "SELECT id, cwd, title, rollout_path FROM threads "
                            "WHERE id IN ({})".format(placeholders),
                            missing,
                        ).fetchall()
                    )
            finally:
                connection.close()
        except (OSError, sqlite3.Error):
            return []

        self._refresh_thread_names()
        result = []
        seen = set()
        for thread_id, cwd, title, rollout_path in rows:
            if not thread_id or not rollout_path or thread_id in seen:
                continue
            seen.add(thread_id)
            result.append(
                _ThreadInfo(
                    id=str(thread_id),
                    cwd=str(cwd or ""),
                    title=self._thread_names.get(
                        str(thread_id), str(title or "Untitled conversation")
                    ),
                    rollout_path=pathlib.Path(str(rollout_path)),
                )
            )
        return result

    def _refresh_thread_names(self) -> None:
        now = time.monotonic()
        if (
            self._thread_names_loaded_at is not None
            and now - self._thread_names_loaded_at < self._thread_name_refresh_seconds
        ):
            return
        self._thread_names_loaded_at = now
        try:
            loaded = self._thread_name_loader()
        except Exception:
            LOGGER.debug("Codex thread name loader failed", exc_info=True)
            return
        if not isinstance(loaded, dict):
            return
        for thread_id, name in loaded.items():
            if isinstance(thread_id, str) and isinstance(name, str) and name.strip():
                self._thread_names[thread_id] = name.strip()

    def _initial_state(self, threads: List[_ThreadInfo]) -> Dict[str, Any]:
        now = _iso(self._now())
        cursors = {}
        for thread in threads:
            try:
                offset = thread.rollout_path.stat().st_size
            except OSError:
                offset = 0
            cursors[thread.id] = {
                "path": str(thread.rollout_path),
                "offset": offset,
            }
        return {
            "version": LEDGER_VERSION,
            "initialized_at": now,
            "last_scan_at": now,
            "revision": 0,
            "cursors": cursors,
            "alerts": [],
        }

    def _scan_threads(self, threads: List[_ThreadInfo]) -> bool:
        changed = False
        cursors = self._state.setdefault("cursors", {})
        last_scan = _parse_timestamp(self._state.get("last_scan_at"))
        for thread in threads:
            existing = cursors.get(thread.id)
            threshold = None
            if existing is None:
                offset = 0
                threshold = last_scan
            else:
                offset = int(existing.get("offset", 0))
                if existing.get("path") != str(thread.rollout_path):
                    offset = 0
                    threshold = last_scan

            lines, new_offset, reset = self._read_complete_lines(
                thread.rollout_path, offset
            )
            if reset:
                threshold = last_scan
            if existing is None or new_offset != offset or reset:
                cursors[thread.id] = {
                    "path": str(thread.rollout_path),
                    "offset": new_offset,
                }
                changed = True
            for line in lines:
                if self._reduce_line(thread, line, threshold):
                    changed = True
        return changed

    def _sync_thread_metadata(self, threads: List[_ThreadInfo]) -> bool:
        current = {thread.id: thread for thread in threads}
        changed = False
        for item in self._state["alerts"]:
            thread = current.get(item.get("thread_id"))
            if thread is None:
                continue
            for key, value in (("project", thread.project), ("title", thread.title)):
                if item.get(key) != value:
                    item[key] = value
                    changed = True
        if changed:
            self._state["revision"] = int(self._state.get("revision", 0)) + 1
        return changed

    def _read_complete_lines(
        self, path: pathlib.Path, offset: int
    ) -> Tuple[List[bytes], int, bool]:
        try:
            size = path.stat().st_size
            reset = offset > size
            if reset:
                offset = 0
            with path.open("rb") as stream:
                stream.seek(offset)
                data = stream.read()
        except OSError:
            return [], offset, False
        last_newline = data.rfind(b"\n")
        if last_newline < 0:
            return [], offset, reset
        complete = data[: last_newline + 1]
        return complete.splitlines(), offset + len(complete), reset

    def _reduce_line(
        self,
        thread: _ThreadInfo,
        line: bytes,
        threshold: Optional[datetime.datetime],
    ) -> bool:
        try:
            item = json.loads(line)
        except (UnicodeDecodeError, json.JSONDecodeError):
            return False
        if not isinstance(item, dict) or item.get("type") != "event_msg":
            return False
        payload = item.get("payload")
        if not isinstance(payload, dict):
            return False
        event_type = payload.get("type")
        if event_type not in ("task_started", "task_complete", "user_message"):
            return False
        timestamp_text = item.get("timestamp") or payload.get("timestamp")
        timestamp = _parse_timestamp(timestamp_text)
        if timestamp is None or (threshold is not None and timestamp <= threshold):
            return False
        timestamp_text = _iso(timestamp)

        if event_type == "task_started":
            alert_id = "{}:{}".format(thread.id, timestamp_text)
            if any(entry.get("id") == alert_id for entry in self._state["alerts"]):
                return False
            self._state["alerts"].append(
                {
                    "id": alert_id,
                    "thread_id": thread.id,
                    "project": thread.project,
                    "title": thread.title,
                    "progress_at": timestamp_text,
                    "handled_at": None,
                    "state": "running",
                    "watch_notified": True,
                    "outcome": "running",
                }
            )
            self._state["revision"] = int(self._state.get("revision", 0)) + 1
            return True

        if event_type == "task_complete":
            outcome = classify_outcome(payload.get("last_agent_message"))
            state = "pending" if outcome == "waiting" else "completed"
            running = []
            for entry in self._state["alerts"]:
                progress = _parse_timestamp(entry.get("progress_at"))
                if (
                    entry.get("thread_id") == thread.id
                    and entry.get("state") == "running"
                    and progress is not None
                    and progress <= timestamp
                ):
                    running.append((progress, entry))
            if running:
                target = max(running, key=lambda candidate: candidate[0])[1]
                target.update(
                    {
                        "id": "{}:{}".format(thread.id, timestamp_text),
                        "project": thread.project,
                        "title": thread.title,
                        "progress_at": timestamp_text,
                        "handled_at": timestamp_text if state == "completed" else None,
                        "state": state,
                        "watch_notified": state != "pending",
                        "outcome": outcome,
                    }
                )
            else:
                alert_id = "{}:{}".format(thread.id, timestamp_text)
                if any(
                    entry.get("id") == alert_id for entry in self._state["alerts"]
                ):
                    return False
                self._state["alerts"].append(
                    {
                        "id": alert_id,
                        "thread_id": thread.id,
                        "project": thread.project,
                        "title": thread.title,
                        "progress_at": timestamp_text,
                        "handled_at": timestamp_text if state == "completed" else None,
                        "state": state,
                        "watch_notified": state != "pending",
                        "outcome": outcome,
                    }
                )
            self._state["revision"] = int(self._state.get("revision", 0)) + 1
            return True

        changed = False
        for entry in self._state["alerts"]:
            progress = _parse_timestamp(entry.get("progress_at"))
            if (
                entry.get("thread_id") == thread.id
                and entry.get("state") == "pending"
                and progress is not None
                and progress < timestamp
            ):
                entry["state"] = "completed"
                entry["outcome"] = "completed"
                entry["handled_at"] = timestamp_text
                entry["watch_notified"] = True
                changed = True
        if changed:
            self._state["revision"] = int(self._state.get("revision", 0)) + 1
        return changed

    def _prune_old_alerts(self) -> bool:
        cutoff = self._now().astimezone(self._local_timezone).date() - datetime.timedelta(
            days=RETENTION_DAYS - 1
        )
        retained = []
        for item in self._state["alerts"]:
            timestamp = _parse_timestamp(item.get("progress_at"))
            if timestamp is None:
                continue
            if timestamp.astimezone(self._local_timezone).date() >= cutoff:
                retained.append(item)
        if len(retained) == len(self._state["alerts"]):
            return False
        self._state["alerts"] = retained
        self._state["revision"] = int(self._state.get("revision", 0)) + 1
        return True

    def _snapshot(self) -> TaskAlertSnapshot:
        today = self._now().astimezone(self._local_timezone).date()
        latest_by_thread = {}
        for item in self._state["alerts"]:
            timestamp = _parse_timestamp(item.get("progress_at"))
            if timestamp is None or timestamp.astimezone(self._local_timezone).date() != today:
                continue
            state = item.get("state")
            if state not in ("pending", "running", "completed"):
                continue
            thread_id = str(item.get("thread_id") or "")
            existing = latest_by_thread.get(thread_id)
            if existing is None or timestamp >= existing[0]:
                latest_by_thread[thread_id] = (timestamp, item)

        alerts = []
        for _timestamp, item in latest_by_thread.values():
            state = item["state"]
            fallback_outcome = {
                "pending": "waiting",
                "running": "running",
                "completed": "completed",
            }[state]
            outcome = item.get("outcome")
            if outcome not in ("completed", "waiting", "running"):
                outcome = fallback_outcome
            alerts.append(
                TaskAlert(
                    id=str(item.get("id") or ""),
                    thread_id=str(item.get("thread_id") or ""),
                    project=str(item.get("project") or "--"),
                    title=str(item.get("title") or "Untitled conversation"),
                    progress_at=str(item.get("progress_at") or ""),
                    handled_at=(
                        str(item["handled_at"]) if item.get("handled_at") else None
                    ),
                    state=state,
                    watch_notified=bool(item.get("watch_notified", False)),
                    outcome=outcome,
                )
            )
        priority = {"pending": 0, "running": 1, "completed": 2}
        alerts.sort(
            key=lambda alert: (
                priority[alert.state],
                -(
                    _parse_timestamp(alert.progress_at)
                    or datetime.datetime.min.replace(tzinfo=UTC)
                ).timestamp(),
            )
        )
        pending = sum(alert.state == "pending" for alert in alerts)
        running = sum(alert.state == "running" for alert in alerts)
        completed = sum(alert.state == "completed" for alert in alerts)
        return TaskAlertSnapshot(
            pending_count=pending,
            running_count=running,
            completed_count=completed,
            handled_count=completed,
            revision=int(self._state.get("revision", 0)),
            alerts=tuple(alerts),
        )

    def _load_state(self) -> Optional[Dict[str, Any]]:
        if not self.ledger_path.exists():
            return None
        try:
            state = json.loads(self.ledger_path.read_text(encoding="utf-8"))
            if (
                not isinstance(state, dict)
                or state.get("version") not in (1, LEDGER_VERSION)
                or not isinstance(state.get("cursors"), dict)
                or not isinstance(state.get("alerts"), list)
            ):
                raise ValueError("invalid alert ledger")
            for item in state["alerts"]:
                if not isinstance(item, dict):
                    raise ValueError("invalid alert ledger entry")
                item_state = item.get("state")
                outcome = item.get("outcome")
                if item_state == "handled" or (
                    item_state == "pending" and outcome == "completed"
                ):
                    item["state"] = "completed"
                    item["outcome"] = "completed"
                    self._needs_save = True
                elif item_state == "running":
                    if outcome != "running":
                        item["outcome"] = "running"
                        self._needs_save = True
                elif item_state == "pending":
                    if outcome != "waiting":
                        item["outcome"] = "waiting"
                        self._needs_save = True
                elif item_state == "completed":
                    if outcome != "completed":
                        item["outcome"] = "completed"
                        self._needs_save = True
                else:
                    raise ValueError("invalid alert ledger state")
            if state.get("version") != LEDGER_VERSION:
                state["version"] = LEDGER_VERSION
                self._needs_save = True
            return state
        except (OSError, ValueError, json.JSONDecodeError):
            suffix = self._now().strftime("%Y%m%dT%H%M%S")
            corrupt = self.ledger_path.with_name(
                self.ledger_path.name + ".corrupt-" + suffix
            )
            try:
                os.replace(self.ledger_path, corrupt)
            except OSError:
                pass
            return None

    def _save_state(self) -> None:
        self.ledger_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.ledger_path.with_suffix(self.ledger_path.suffix + ".tmp")
        temporary.write_text(
            json.dumps(self._state, indent=2, ensure_ascii=False),
            encoding="utf-8",
        )
        os.replace(temporary, self.ledger_path)
        self._needs_save = False
