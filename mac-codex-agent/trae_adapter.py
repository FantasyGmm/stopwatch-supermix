"""Collect a bounded, privacy-preserving Trae status snapshot."""

from __future__ import annotations

import dataclasses
import datetime
import hashlib
import json
import mmap
import os
import pathlib
import re
import sqlite3
import time
import unicodedata
import urllib.parse
from typing import Callable, Dict, Iterable, List, Mapping, Optional, Tuple

from trae_tasks import TraeTask, TraeTaskTracker, ordered_active_tasks


_TRAE_BUNDLE_ID = "cn.trae.app"
_CURRENT_AGENT_KEY = "trae.currentAgent"
_ACTIVE_METADATA_STATES = {"pending", "running"}
_TERMINAL_METADATA_STATES = {"completed", "cancelled", "failed"}
_SESSION_MODEL_MAP_KEY = "lukeren_ai-chat:sessionRelation:modelMap"
_GLOBAL_MODEL_MAP_KEY = "lukeren_ai-chat:sessionRelation:globalModelMap"
_MODEL_LIST_MAP_KEY = "lukeren_AI.agent.model.model_list_map"
_SESSION_STORAGE_KEY = "memento/icube-ai-agent-storage"
_TITLE_MARKER = b"generate_session_title_and_icon_cloud:"
_TRAE_PROCESS_LOCK = (
    pathlib.Path.home() / "Library" / "Application Support" / "Trae CN" / "code.lock"
)
_TITLE_RE = re.compile(
    r'generate_session_title_and_icon_cloud:\s*session_id:\s*"(?P<session>[^"]+)"'
    r',\s*title:\s*"(?P<title>(?:\\.|[^"\\])*)"'
)
_MODEL_PREFIX_RE = re.compile(r"^\d+_-_")


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
    workspace_result = None
    try:
        from AppKit import NSWorkspace

        if any(
            application.bundleIdentifier() == _TRAE_BUNDLE_ID
            for application in NSWorkspace.sharedWorkspace().runningApplications()
        ):
            return True
        workspace_result = False
    except (ImportError, AttributeError, TypeError):
        pass

    try:
        process_id = int(
            _TRAE_PROCESS_LOCK.read_text(encoding="utf-8").strip()
        )
        if process_id <= 0:
            return workspace_result
        os.kill(process_id, 0)
        return True
    except FileNotFoundError:
        return False
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except (OSError, TypeError, ValueError):
        return workspace_result


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
        self._session_titles: Dict[str, str] = {}
        self._title_retry_after: Dict[str, float] = {}

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
        metadata_observations = (
            self._current_agent_metadata()
            + self._real_session_metadata(snapshot.tasks, now)
        )
        fallback_project = self._workspace_project()
        latest_metadata_model = max(
            (
                item
                for item in metadata_observations
                if _clean_text(item[1].get("selected_model"), 64)
            ),
            default=None,
            key=lambda item: (
                item[0],
                item[1].get("session_id", ""),
                item[1].get("task_id", ""),
            ),
        )
        metadata_is_newer = latest_metadata_model is not None and (
            snapshot.model is None
            or (latest_metadata_model[0] / 1_000_000_000.0)
            > snapshot.model_updated_at
        )
        if metadata_is_newer:
            latest_metadata = latest_metadata_model[1]
            model = _clean_text(latest_metadata.get("selected_model"), 64)
            effort = _clean_text(latest_metadata.get("selected_effort"), 32)
        else:
            model = snapshot.model
            effort = snapshot.effort

        enriched_tasks = snapshot.tasks
        for observed_at_ns, metadata in metadata_observations:
            enriched_tasks = self._enrich_tasks(
                enriched_tasks,
                metadata,
                fallback_project,
                observed_at_ns / 1_000_000_000.0 if observed_at_ns > 0 else now,
            )
        remember_tasks = getattr(self._tracker, "remember_tasks", None)
        if callable(remember_tasks):
            snapshot = remember_tasks(enriched_tasks)
            enriched_tasks = snapshot.tasks
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

    def _current_agent_metadata(
        self,
    ) -> Tuple[Tuple[int, Dict[str, object]], ...]:
        candidates = self._sandbox_metadata() + self._state_db_metadata()
        newest_by_task = {}
        for candidate in candidates:
            value = candidate[1]
            key = (value.get("session_id"), value.get("task_id"))
            previous = newest_by_task.get(key)
            if previous is None or candidate[0] >= previous[0]:
                newest_by_task[key] = candidate
        return tuple(
            sorted(
                newest_by_task.values(),
                key=lambda item: (
                    item[0],
                    item[1].get("session_id", ""),
                    item[1].get("task_id", ""),
                ),
            )
        )

    def _sandbox_metadata(self) -> List[Tuple[int, Dict[str, object]]]:
        if not self._paths.sandbox.is_dir():
            return []
        candidates = []
        try:
            paths = sorted(self._paths.sandbox.rglob("*.json"), key=str)
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
            paths = sorted(
                self._paths.workspace_storage.rglob("state.vscdb"),
                key=str,
            )
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
        return TraeStateCollector._read_json_row(path, _CURRENT_AGENT_KEY)

    @staticmethod
    def _read_json_row(path: pathlib.Path, key: str) -> object:
        connection = None
        try:
            uri = "file:{}?mode=ro".format(
                urllib.parse.quote(str(path), safe="/:")
            )
            connection = sqlite3.connect(uri, uri=True)
            row = connection.execute(
                "SELECT value FROM ItemTable WHERE key = ? LIMIT 1",
                (key,),
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

    def _real_session_metadata(
        self,
        tasks: Iterable[TraeTask],
        now: float,
    ) -> Tuple[Tuple[int, Dict[str, object]], ...]:
        active = tuple(
            task for task in tasks if task.state in _ACTIVE_METADATA_STATES
        )
        if not active or not self._paths.workspace_storage.is_dir():
            return ()

        titles = self._titles_for_sessions(
            {task.session_id for task in active}, now
        )
        model_catalog = self._model_catalog()
        observations = []
        try:
            state_paths = sorted(
                self._paths.workspace_storage.rglob("state.vscdb"), key=str
            )
        except OSError:
            return ()

        for state_path in state_paths:
            model_map = self._read_json_row(state_path, _SESSION_MODEL_MAP_KEY)
            if not isinstance(model_map, Mapping):
                model_map = {}
            session_storage = self._read_json_row(
                state_path, _SESSION_STORAGE_KEY
            )
            workspace_sessions = set(model_map)
            if isinstance(session_storage, Mapping):
                raw_sessions = session_storage.get("list")
                if isinstance(raw_sessions, list):
                    workspace_sessions.update(
                        item.get("sessionId")
                        for item in raw_sessions
                        if isinstance(item, Mapping)
                        and isinstance(item.get("sessionId"), str)
                    )
            global_model = self._read_json_row(
                state_path, _GLOBAL_MODEL_MAP_KEY
            )
            workspace_path = state_path.with_name("workspace.json")
            project_path = ""
            try:
                workspace = json.loads(
                    workspace_path.read_text(encoding="utf-8")
                )
                if isinstance(workspace, Mapping):
                    project_path = _clean_text(workspace.get("folder"), 512)
            except (OSError, ValueError, TypeError, json.JSONDecodeError):
                pass
            try:
                observed_at_ns = max(
                    state_path.stat().st_mtime_ns,
                    workspace_path.stat().st_mtime_ns
                    if workspace_path.is_file()
                    else 0,
                )
            except OSError:
                observed_at_ns = 0

            for task in active:
                if task.session_id not in workspace_sessions:
                    continue
                raw_selection = model_map.get(task.session_id)
                if not isinstance(raw_selection, Mapping):
                    raw_selection = global_model
                model_name = self._selected_model_name(raw_selection)
                display_model, effort = model_catalog.get(
                    model_name, (model_name, "")
                )
                observations.append(
                    (
                        observed_at_ns,
                        self._normalize_metadata(
                            {
                                "session_id": task.session_id,
                                "task_id": task.task_id,
                                "selected_model": display_model,
                                "selected_effort": effort,
                                "project_path": project_path,
                                "task_title": titles.get(task.session_id, ""),
                                "state": task.state,
                            }
                        ),
                    )
                )
        return tuple(item for item in observations if item[1])

    @staticmethod
    def _selected_model_name(value: object) -> str:
        if not isinstance(value, Mapping):
            return ""
        raw = value.get("solo_coder")
        if not isinstance(raw, str):
            raw = next(
                (item for _, item in sorted(value.items()) if isinstance(item, str)),
                "",
            )
        return _clean_text(_MODEL_PREFIX_RE.sub("", raw), 64)

    def _model_catalog(self) -> Dict[str, Tuple[str, str]]:
        path = (
            self._paths.workspace_storage.parent
            / "globalStorage"
            / "state.vscdb"
        )
        value = self._read_json_row(path, _MODEL_LIST_MAP_KEY)
        if not isinstance(value, Mapping):
            return {}
        catalog = {}
        groups = list(value.items())
        groups.sort(key=lambda item: (item[0] != "solo_agent", item[0]))
        for _, records in groups:
            if not isinstance(records, list):
                continue
            for record in records:
                if not isinstance(record, Mapping):
                    continue
                name = _clean_text(record.get("name"), 64)
                if not name or name in catalog:
                    continue
                catalog[name] = (
                    _clean_text(record.get("display_name"), 64) or name,
                    _clean_text(record.get("default_reasoning_effort"), 32),
                )
        return catalog

    def _titles_for_sessions(
        self, session_ids: Iterable[str], now: float
    ) -> Dict[str, str]:
        requested = {item for item in session_ids if item}
        missing = {
            item
            for item in requested
            if item not in self._session_titles
            and now >= self._title_retry_after.get(item, 0.0)
        }
        if missing:
            self._scan_session_titles(missing)
            for session_id in missing:
                if session_id not in self._session_titles:
                    self._title_retry_after[session_id] = now + 30.0
        return {
            session_id: self._session_titles[session_id]
            for session_id in requested
            if session_id in self._session_titles
        }

    def _scan_session_titles(self, session_ids: Iterable[str]) -> None:
        pending = set(session_ids)
        for path in self._log_paths():
            if not pending or (
                "ai-agent" not in path.name and path.name != "agent.log"
            ):
                continue
            try:
                with path.open("rb") as stream:
                    if path.stat().st_size == 0:
                        continue
                    with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
                        offset = 0
                        while pending:
                            marker = data.find(_TITLE_MARKER, offset)
                            if marker < 0:
                                break
                            line_start = data.rfind(b"\n", 0, marker) + 1
                            line_end = data.find(b"\n", marker)
                            if line_end < 0:
                                line_end = len(data)
                            offset = line_end + 1
                            if line_end - line_start > 4096:
                                continue
                            line = data[line_start:line_end].decode(
                                "utf-8", errors="replace"
                            )
                            match = _TITLE_RE.search(line)
                            if match is None or match.group("session") not in pending:
                                continue
                            try:
                                title = json.loads(
                                    '"{}"'.format(match.group("title"))
                                )
                            except (ValueError, TypeError, json.JSONDecodeError):
                                title = match.group("title")
                            cleaned = _clean_text(title, 48)
                            if cleaned:
                                session_id = match.group("session")
                                self._session_titles[session_id] = cleaned
                                pending.remove(session_id)
            except (OSError, ValueError):
                continue

    @staticmethod
    def _normalize_metadata(value: object) -> Dict[str, object]:
        if not isinstance(value, Mapping):
            return {}
        session_id = _clean_text(value.get("session_id"), 128)
        task_id = _clean_text(value.get("task_id"), 128)
        if not session_id or not task_id:
            return {}
        raw_state = value.get("state")
        state = raw_state if raw_state in _ACTIVE_METADATA_STATES else (
            "completed" if raw_state in _TERMINAL_METADATA_STATES else "running"
        )
        return {
            "session_id": session_id,
            "task_id": task_id,
            "selected_model": _clean_text(value.get("selected_model"), 64),
            "selected_effort": _clean_text(value.get("selected_effort"), 32),
            "project_path": _clean_text(value.get("project_path"), 512),
            "task_title": _clean_text(value.get("task_title"), 48),
            "task_detail": _clean_text(value.get("task_detail"), 72),
            "state": state,
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
                metadata_is_newer = now > task.updated_at
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
                        state=(
                            metadata.get("state", task.state)
                            if metadata_is_newer
                            else task.state
                        ),
                        updated_at=now if metadata_is_newer else task.updated_at,
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
