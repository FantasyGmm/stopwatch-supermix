"""Collect a bounded, privacy-preserving Trae status snapshot."""

from __future__ import annotations

import dataclasses
import datetime
import hashlib
import json
import pathlib
import sqlite3
import time
import unicodedata
import urllib.parse
from typing import Callable, Dict, Iterable, List, Mapping, Optional, Tuple

from trae_tasks import TraeTask, TraeTaskTracker, ordered_active_tasks


_TRAE_BUNDLE_ID = "cn.trae.app"
_CURRENT_AGENT_KEY = "trae.currentAgent"
_VALID_STATES = {"pending", "running", "completed"}


@dataclasses.dataclass(frozen=True)
class TraePaths:
    logs: pathlib.Path
    workspace_storage: pathlib.Path
    sandbox: pathlib.Path
    cursor: pathlib.Path
    ledger: pathlib.Path


def _default_paths() -> TraePaths:
    support = pathlib.Path.home() / "Library" / "Application Support" / "Trae CN"
    cache = pathlib.Path.home() / ".trae-cn"
    return TraePaths(
        logs=support / "logs",
        workspace_storage=support / "User" / "workspaceStorage",
        sandbox=support / "ModularData" / "ai-agent" / "sandbox",
        cursor=cache / ".stopwatch_cursor.json",
        ledger=cache / ".stopwatch_tasks.json",
    )


DEFAULT_PATHS = _default_paths()


def trae_process_is_running() -> Optional[bool]:
    try:
        from AppKit import NSWorkspace

        return any(
            application.bundleIdentifier() == _TRAE_BUNDLE_ID
            for application in NSWorkspace.sharedWorkspace().runningApplications()
        )
    except (ImportError, AttributeError, TypeError):
        return None


def _clean_text(value: object, limit: int) -> str:
    if not isinstance(value, str):
        return ""
    printable = "".join(
        " " if unicodedata.category(character).startswith("C") else character
        for character in value
    )
    return " ".join(printable.split())[:limit]


def _project_name(value: object) -> str:
    cleaned = _clean_text(value, 512)
    if not cleaned:
        return ""
    parsed = urllib.parse.urlparse(cleaned)
    path = urllib.parse.unquote(parsed.path) if parsed.scheme else cleaned
    return _clean_text(pathlib.PurePath(path.rstrip("/")).name, 32)


class TraeStateCollector:
    def __init__(
        self,
        paths: TraePaths = DEFAULT_PATHS,
        process_is_running: Callable[[], Optional[bool]] = trae_process_is_running,
        now: Callable[[], float] = time.time,
    ):
        self._paths = paths
        self._process_is_running = process_is_running
        self._now = now
        self._tracker = TraeTaskTracker(paths.cursor, paths.ledger)

    def get_state(self) -> Dict[str, object]:
        process_result = self._process_is_running()
        process_state = "live" if process_result is True else (
            "off" if process_result is False else "unknown"
        )
        if process_result is not True:
            return {
                "trae_online": False,
                "trae_process_state": process_state,
                "trae_model": "--",
                "trae_effort": "--",
                "trae_revision": 0,
                "trae_pending_count": 0,
                "trae_running_count": 0,
                "trae_tasks_truncated": False,
                "trae_tasks": [],
            }

        now = self._now()
        snapshot = self._tracker.refresh(self._log_paths(), now)
        metadata = self._current_agent_metadata()
        fallback_project = self._workspace_project()
        model = snapshot.model or _clean_text(metadata.get("selected_model"), 64)
        effort = (
            snapshot.effort
            if snapshot.model is not None
            else _clean_text(metadata.get("selected_effort"), 32)
        )
        enriched_tasks = self._enrich_tasks(
            snapshot.tasks,
            metadata,
            fallback_project,
            now,
        )
        active_tasks = ordered_active_tasks(enriched_tasks)
        transport_tasks = active_tasks[:4]
        pending_count = sum(item.state == "pending" for item in active_tasks)
        running_count = sum(item.state == "running" for item in active_tasks)
        return {
            "trae_online": True,
            "trae_process_state": "live",
            "trae_model": model or "--",
            "trae_effort": effort or "--",
            "trae_revision": snapshot.revision,
            "trae_pending_count": pending_count,
            "trae_running_count": running_count,
            "trae_tasks_truncated": len(active_tasks) > len(transport_tasks),
            "trae_tasks": [
                self._task_payload(task, model or "", effort or "")
                for task in transport_tasks
            ],
        }

    def _log_paths(self) -> Tuple[pathlib.Path, ...]:
        if self._paths.logs.is_file():
            return (self._paths.logs,)
        if not self._paths.logs.is_dir():
            return ()
        try:
            return tuple(
                path
                for path in self._paths.logs.rglob("*.log")
                if path.is_file()
            )
        except OSError:
            return ()

    def _current_agent_metadata(self) -> Dict[str, object]:
        candidates = self._sandbox_metadata() + self._state_db_metadata()
        if not candidates:
            return {}
        _, value = max(candidates, key=lambda item: item[0])
        return value

    def _sandbox_metadata(self) -> List[Tuple[int, Dict[str, object]]]:
        if not self._paths.sandbox.is_dir():
            return []
        candidates = []
        try:
            paths = self._paths.sandbox.rglob("*.json")
            for path in paths:
                try:
                    value = json.loads(path.read_text(encoding="utf-8"))
                    normalized = self._normalize_metadata(value)
                    if normalized:
                        candidates.append((path.stat().st_mtime_ns, normalized))
                except (OSError, ValueError, TypeError, json.JSONDecodeError):
                    continue
        except OSError:
            return []
        return candidates

    def _state_db_metadata(self) -> List[Tuple[int, Dict[str, object]]]:
        if not self._paths.workspace_storage.is_dir():
            return []
        candidates = []
        try:
            paths = self._paths.workspace_storage.rglob("state.vscdb")
            for path in paths:
                value = self._read_current_agent_row(path)
                normalized = self._normalize_metadata(value)
                if normalized:
                    try:
                        candidates.append((path.stat().st_mtime_ns, normalized))
                    except OSError:
                        continue
        except OSError:
            return []
        return candidates

    @staticmethod
    def _read_current_agent_row(path: pathlib.Path) -> object:
        connection = None
        try:
            uri = "file:{}?mode=ro".format(
                urllib.parse.quote(str(path), safe="/:")
            )
            connection = sqlite3.connect(uri, uri=True)
            row = connection.execute(
                "SELECT value FROM ItemTable WHERE key = ? LIMIT 1",
                (_CURRENT_AGENT_KEY,),
            ).fetchone()
            if row is None:
                return None
            raw = row[0]
            if isinstance(raw, bytes):
                raw = raw.decode("utf-8", errors="replace")
            return json.loads(raw) if isinstance(raw, str) else None
        except (OSError, sqlite3.Error, ValueError, TypeError, json.JSONDecodeError):
            return None
        finally:
            if connection is not None:
                connection.close()

    @staticmethod
    def _normalize_metadata(value: object) -> Dict[str, object]:
        if not isinstance(value, Mapping):
            return {}
        session_id = _clean_text(value.get("session_id"), 128)
        task_id = _clean_text(value.get("task_id"), 128)
        if not session_id or not task_id:
            return {}
        return {
            "session_id": session_id,
            "task_id": task_id,
            "selected_model": _clean_text(value.get("selected_model"), 64),
            "selected_effort": _clean_text(value.get("selected_effort"), 32),
            "project_path": _clean_text(value.get("project_path"), 512),
            "task_title": _clean_text(value.get("task_title"), 48),
            "task_detail": _clean_text(value.get("task_detail"), 72),
            "state": (
                value.get("state")
                if value.get("state") in _VALID_STATES
                else "running"
            ),
        }

    def _workspace_project(self) -> str:
        if not self._paths.workspace_storage.is_dir():
            return ""
        candidates = []
        try:
            paths = self._paths.workspace_storage.rglob("workspace.json")
            for path in paths:
                try:
                    value = json.loads(path.read_text(encoding="utf-8"))
                    project = _project_name(value.get("folder"))
                    if project:
                        candidates.append((path.stat().st_mtime_ns, project))
                except (OSError, ValueError, TypeError, json.JSONDecodeError):
                    continue
        except OSError:
            return ""
        return max(candidates, default=(0, ""), key=lambda item: item[0])[1]

    @staticmethod
    def _enrich_tasks(
        tasks: Iterable[TraeTask],
        metadata: Mapping[str, object],
        fallback_project: str,
        now: float,
    ) -> Tuple[TraeTask, ...]:
        session_id = metadata.get("session_id")
        task_id = metadata.get("task_id")
        matched = False
        enriched = []
        for task in tasks:
            if task.session_id == session_id and task.task_id == task_id:
                matched = True
                enriched.append(
                    TraeTask(
                        session_id=task.session_id,
                        task_id=task.task_id,
                        project=_project_name(metadata.get("project_path"))
                        or fallback_project
                        or task.project,
                        title=_clean_text(metadata.get("task_title"), 48)
                        or task.title,
                        detail=_clean_text(metadata.get("task_detail"), 72)
                        or task.detail,
                        state=task.state,
                        updated_at=task.updated_at,
                        model=task.model
                        or _clean_text(metadata.get("selected_model"), 64),
                        effort=task.effort
                        or _clean_text(metadata.get("selected_effort"), 32),
                    )
                )
            else:
                enriched.append(task)

        if not matched and isinstance(session_id, str) and isinstance(task_id, str):
            enriched.append(
                TraeTask(
                    session_id=session_id,
                    task_id=task_id,
                    project=_project_name(metadata.get("project_path"))
                    or fallback_project
                    or "--",
                    title=_clean_text(metadata.get("task_title"), 48)
                    or "Trae task",
                    detail=_clean_text(metadata.get("task_detail"), 72),
                    state=metadata.get("state", "running"),
                    updated_at=now,
                    model=_clean_text(metadata.get("selected_model"), 64),
                    effort=_clean_text(metadata.get("selected_effort"), 32),
                )
            )
        return tuple(enriched)

    @staticmethod
    def _task_payload(
        task: TraeTask,
        snapshot_model: str,
        snapshot_effort: str,
    ) -> Dict[str, object]:
        timestamp = datetime.datetime.fromtimestamp(task.updated_at)
        return {
            "id": hashlib.sha256(
                (task.session_id + "\0" + task.task_id).encode("utf-8")
            ).hexdigest()[:16],
            "project": _clean_text(task.project, 32) or "--",
            "title": _clean_text(task.title, 48) or "Trae task",
            "detail": _clean_text(task.detail, 72),
            "time": timestamp.strftime("%H:%M"),
            "state": task.state,
            "model": _clean_text(task.model, 64)
            or _clean_text(snapshot_model, 64)
            or "--",
            "effort": _clean_text(task.effort, 32)
            or _clean_text(snapshot_effort, 32)
            or "--",
        }
