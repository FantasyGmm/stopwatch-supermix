"""Parse and normalize the small Trae task surface sent to the watch."""

from __future__ import annotations

import dataclasses
import datetime
import json
import os
import pathlib
import re
from typing import Dict, Iterable, Literal, Optional, Sequence, Tuple


_IDENTIFIER = r"[A-Za-z0-9_.:-]{1,128}"
_SESSION_RE = re.compile(r"\bsession_id=(?P<value>{})\b".format(_IDENTIFIER))
_TASK_RE = re.compile(r"\btask_id=(?P<value>{})\b".format(_IDENTIFIER))
_TIMESTAMP_RE = re.compile(
    r"^(?P<value>\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}"
    r"(?:[.,]\d{1,6})?(?:Z|[+-]\d{2}:\d{2})?)"
)
_MODEL_RE = re.compile(
    r'model_info:\s*CustomModel\s*\{[^}\r\n]{0,512}?'
    r'display_model_name:\s*Some\("(?P<model>[^"\r\n]{1,64})"\)'
    r'[^}\r\n]{0,512}?reasoning_effort:\s*'
    r'(?:(?:Some\("(?P<effort>[^"\r\n]{1,32})"\))|None)'
    r"[^}\r\n]{0,512}?\}"
)
_TIMING_MODEL_RE = re.compile(
    r'TimingCostOriginEvent[^\r\n]{0,4096}?'
    r'\\"provider_model_name\\":\\"'
    r'(?P<model>[^"\\\r\n]{1,64})\\"'
)
_TASK_STATUS_RE = re.compile(
    r"ai_agent::domain::task::cloud_service:\s+TASK:\s+"
    r"[^\r\n]{0,1024}?\bstatus=(?P<status>Created|Completed)\b",
    re.IGNORECASE,
)


@dataclasses.dataclass(frozen=True)
class TraeLogEvent:
    kind: Literal["started", "progress", "pending", "completed", "model"]
    session_id: Optional[str] = None
    task_id: Optional[str] = None
    model: Optional[str] = None
    effort: Optional[str] = None
    occurred_at: Optional[float] = None


@dataclasses.dataclass(frozen=True)
class TraeTask:
    session_id: str
    task_id: str
    project: str
    title: str
    detail: str
    state: Literal["pending", "running", "completed"]
    updated_at: float
    model: str = ""
    effort: str = ""


@dataclasses.dataclass(frozen=True)
class TraeLogCursor:
    path: str
    inode: int
    offset: int
    mtime_ns: int
    model: str = ""
    effort: str = ""
    model_updated_at: float = 0.0


@dataclasses.dataclass(frozen=True)
class TraeTaskSnapshot:
    tasks: Tuple[TraeTask, ...]
    model: Optional[str]
    effort: Optional[str]
    revision: int
    model_updated_at: float = 0.0


def parse_trae_log_event(line: str) -> Optional[TraeLogEvent]:
    if not isinstance(line, str) or not line:
        return None

    occurred_at = None
    timestamp_match = _TIMESTAMP_RE.search(line)
    if timestamp_match is not None:
        timestamp = timestamp_match.group("value").replace(",", ".")
        try:
            occurred_at = datetime.datetime.fromisoformat(
                timestamp.replace("Z", "+00:00")
            ).timestamp()
        except (OverflowError, ValueError):
            occurred_at = None

    timing_model_match = _TIMING_MODEL_RE.search(line)
    if timing_model_match is not None:
        session_match = _SESSION_RE.search(line)
        task_match = _TASK_RE.search(line)
        return TraeLogEvent(
            kind="model",
            session_id=(
                session_match.group("value")
                if session_match is not None
                else None
            ),
            task_id=(
                task_match.group("value") if task_match is not None else None
            ),
            model=timing_model_match.group("model"),
            occurred_at=occurred_at,
        )

    model_match = _MODEL_RE.search(line)
    if model_match is not None:
        return TraeLogEvent(
            kind="model",
            model=model_match.group("model"),
            effort=model_match.group("effort"),
            occurred_at=occurred_at,
        )

    task_status_match = _TASK_STATUS_RE.search(line)
    kind = None
    if task_status_match is not None:
        kind = (
            "started"
            if task_status_match.group("status").lower() == "created"
            else "completed"
        )
    elif "progress_notice" in line:
        kind = "progress"
    elif "ToolCall arrived name=AskUserQuestion" in line:
        kind = "pending"
    elif "[append_resume_listener] exiting" in line:
        kind = "completed"
    if kind is None:
        return None

    session_match = _SESSION_RE.search(line)
    task_match = _TASK_RE.search(line)
    if session_match is None or task_match is None:
        return None
    return TraeLogEvent(
        kind=kind,
        session_id=session_match.group("value"),
        task_id=task_match.group("value"),
        occurred_at=occurred_at,
    )


def apply_trae_log_event(
    tasks: Iterable[TraeTask],
    event: TraeLogEvent,
    now: float,
    model: Optional[str] = None,
    effort: Optional[str] = None,
) -> Tuple[TraeTask, ...]:
    current = list(tasks)
    if event.session_id is None or event.task_id is None:
        return tuple(current)

    matching_index = next(
        (
            index
            for index, item in enumerate(current)
            if item.session_id == event.session_id and item.task_id == event.task_id
        ),
        None,
    )
    existing = current[matching_index] if matching_index is not None else None

    if event.kind == "model":
        if existing is None or event.model is None:
            return tuple(current)
        current[matching_index] = dataclasses.replace(
            existing,
            model=event.model,
            effort=(
                event.effort if event.effort is not None else existing.effort
            ),
        )
        return tuple(current)

    if event.kind == "progress" and existing is None:
        return tuple(current)

    if event.kind == "completed" and existing is not None and existing.state == "pending":
        return tuple(current)

    state = "completed" if event.kind == "completed" else (
        "pending" if event.kind == "pending" else "running"
    )
    replacement = TraeTask(
        session_id=event.session_id,
        task_id=event.task_id,
        project=existing.project if existing is not None else "--",
        title=existing.title if existing is not None else "Trae task",
        detail=existing.detail if existing is not None else "",
        state=state,
        updated_at=now,
        model=(
            model
            if model is not None
            else (existing.model if existing is not None else "")
        ),
        effort=(
            (effort or "")
            if model is not None
            else (existing.effort if existing is not None else "")
        ),
    )
    if matching_index is None:
        current.append(replacement)
    else:
        current[matching_index] = replacement
    return tuple(current)


def ordered_active_tasks(tasks: Iterable[TraeTask]) -> Tuple[TraeTask, ...]:
    state_order = {"pending": 0, "running": 1}
    return tuple(
        sorted(
            (item for item in tasks if item.state in state_order),
            key=lambda item: (
                state_order[item.state],
                -item.updated_at,
                item.session_id,
                item.task_id,
            ),
        )
    )


def select_transport_tasks(
    tasks: Iterable[TraeTask], limit: int = 4
) -> Tuple[TraeTask, ...]:
    return ordered_active_tasks(tasks)[: max(0, limit)]


class TraeTaskTracker:
    _METADATA_TAIL_BYTES = 256 * 1024
    _TASK_METADATA_RECOVERY_BYTES = 8 * 1024 * 1024
    _MAX_LEDGER_TASKS = 32
    _LEDGER_VERSION = 4

    def __init__(self, cursor_path: pathlib.Path, ledger_path: pathlib.Path):
        self._cursor_path = pathlib.Path(cursor_path)
        self._ledger_path = pathlib.Path(ledger_path)
        self._cursors = self._load_cursors()
        (
            self._tasks,
            self._model,
            self._effort,
            self._model_updated_at,
            self._revision,
            migrate_ledger,
        ) = self._load_ledger()
        if migrate_ledger:
            self._revision += 1
            self._save_ledger()

    def refresh(
        self,
        log_paths: Sequence[pathlib.Path],
        now: float,
    ) -> TraeTaskSnapshot:
        paths = self._existing_logs(log_paths)
        if not paths:
            return self._snapshot()

        changed = False
        activity_events = []
        model_events = []
        for path in paths:
            try:
                stat = path.stat()
            except OSError:
                continue
            key = str(path)
            cursor = self._cursors.get(key)
            requires_baseline = (
                cursor is None
                or cursor.path != key
                or cursor.inode != stat.st_ino
                or stat.st_size < cursor.offset
            )
            if requires_baseline:
                model_event = self._baseline(path, stat)
                if model_event is not None:
                    model_event_time = self._event_time(
                        model_event, stat.st_mtime_ns, 0
                    )
                    model_events.append(
                        (
                            model_event_time,
                            key,
                            0,
                            model_event,
                        )
                    )
                    updated = apply_trae_log_event(
                        self._tasks,
                        model_event,
                        model_event_time,
                    )
                    if updated != self._tasks:
                        self._tasks = updated
                        changed = True
                continue

            recovery_is_relevant = not cursor.model and any(
                task.state != "completed"
                and (stat.st_mtime_ns / 1_000_000_000.0) >= task.updated_at
                for task in self._tasks
            )
            recovered_task_model = (
                self._latest_correlated_model_from_tail(path, cursor.offset)
                if recovery_is_relevant
                else None
            )
            local_model = recovered_task_model or (
                TraeLogEvent(
                    kind="model",
                    model=cursor.model,
                    effort=cursor.effort or None,
                    occurred_at=(
                        cursor.model_updated_at
                        if cursor.model_updated_at > 0
                        else None
                    ),
                )
                if cursor.model
                else self._latest_model_from_tail(path, cursor.offset)
            )
            local_model_updated_at = 0.0
            if local_model is not None:
                local_model_updated_at = self._event_time(
                    local_model, cursor.mtime_ns, 0
                )
                model_events.append(
                    (
                        local_model_updated_at,
                        key,
                        0,
                        local_model,
                    )
                )
                updated = apply_trae_log_event(
                    self._tasks,
                    local_model,
                    local_model_updated_at,
                )
                if updated != self._tasks:
                    self._tasks = updated
                    changed = True
            try:
                with path.open("rb") as stream:
                    stream.seek(cursor.offset)
                    appended = stream.read()
            except OSError:
                continue
            complete_length = appended.rfind(b"\n") + 1
            if complete_length > 0:
                decoded_lines = appended[:complete_length].decode(
                    "utf-8", errors="replace"
                ).splitlines()
                for line_index, line in enumerate(decoded_lines, start=1):
                    event = parse_trae_log_event(line)
                    if event is None:
                        continue
                    if event.kind == "model":
                        local_model = event
                        local_model_updated_at = self._event_time(
                            event, stat.st_mtime_ns, line_index
                        )
                        model_events.append(
                            (
                                local_model_updated_at,
                                key,
                                line_index,
                                event,
                            )
                        )
                        if event.session_id is not None and event.task_id is not None:
                            activity_events.append(
                                (
                                    local_model_updated_at,
                                    key,
                                    line_index,
                                    event,
                                    local_model,
                                )
                            )
                        continue
                    activity_events.append(
                        (
                            self._event_time(event, stat.st_mtime_ns, line_index),
                            key,
                            line_index,
                            event,
                            local_model,
                        )
                    )

            self._cursors[key] = TraeLogCursor(
                path=key,
                inode=stat.st_ino,
                offset=cursor.offset + complete_length,
                mtime_ns=stat.st_mtime_ns,
                model=local_model.model if local_model is not None else "",
                effort=(
                    (local_model.effort or "")
                    if local_model is not None
                    else ""
                ),
                model_updated_at=local_model_updated_at,
            )

        if model_events:
            latest_model_entry = max(
                model_events,
                key=lambda item: (item[0], item[1], item[2]),
            )
            latest_model_time = latest_model_entry[0]
            latest_model = latest_model_entry[3]
            next_pair = (latest_model.model, latest_model.effort)
            if latest_model_time > self._model_updated_at or (
                self._model is None and latest_model_time == self._model_updated_at
            ):
                model_changed = next_pair != (self._model, self._effort)
                self._model, self._effort = next_pair
                timestamp_changed = latest_model_time != self._model_updated_at
                self._model_updated_at = latest_model_time
                changed = changed or model_changed or timestamp_changed

        for activity_time, _path_key, _line_index, event, local_model in sorted(
            activity_events,
            key=lambda item: (item[0], item[1], item[2]),
        ):
            updated = apply_trae_log_event(
                self._tasks,
                event,
                activity_time,
                model=local_model.model if local_model is not None else None,
                effort=local_model.effort if local_model is not None else None,
            )
            if updated != self._tasks:
                self._tasks = updated
                changed = True

        if changed:
            self._revision += 1
            self._tasks = self._retain_ledger_tasks(self._tasks)
            self._save_ledger()
        self._save_cursors()
        return self._snapshot()

    def _snapshot(self) -> TraeTaskSnapshot:
        return TraeTaskSnapshot(
            tasks=self._tasks,
            model=self._model,
            effort=self._effort,
            revision=self._revision,
            model_updated_at=self._model_updated_at,
        )

    def remember_tasks(self, tasks: Iterable[TraeTask]) -> TraeTaskSnapshot:
        retained = self._retain_ledger_tasks(tasks)
        if retained != self._tasks:
            self._tasks = retained
            self._revision += 1
            self._save_ledger()
        return self._snapshot()

    @staticmethod
    def _existing_logs(
        log_paths: Sequence[pathlib.Path],
    ) -> Tuple[pathlib.Path, ...]:
        return tuple(
            sorted(
                {
                    pathlib.Path(path)
                    for path in log_paths
                    if pathlib.Path(path).is_file()
                },
                key=str,
            )
        )

    def _baseline(
        self, path: pathlib.Path, stat: os.stat_result
    ) -> Optional[TraeLogEvent]:
        model = self._latest_model_from_tail(path, stat.st_size)
        model_updated_at = (
            self._event_time(model, stat.st_mtime_ns, 0)
            if model is not None
            else 0.0
        )
        key = str(path)
        self._cursors[key] = TraeLogCursor(
            path=key,
            inode=stat.st_ino,
            offset=stat.st_size,
            mtime_ns=stat.st_mtime_ns,
            model=(
                model.model
                if model is not None and model.model is not None
                else ""
            ),
            effort=(model.effort or "") if model is not None else "",
            model_updated_at=model_updated_at,
        )
        return model

    @staticmethod
    def _event_time(
        event: TraeLogEvent,
        mtime_ns: int,
        line_index: int,
    ) -> float:
        if event.occurred_at is not None:
            return event.occurred_at
        return (mtime_ns / 1_000_000_000.0) + (line_index / 1_000_000.0)

    def _latest_model_from_tail(
        self,
        path: pathlib.Path,
        size: int,
    ) -> Optional[TraeLogEvent]:
        start = max(0, size - self._METADATA_TAIL_BYTES)
        with path.open("rb") as stream:
            stream.seek(start)
            tail = stream.read(size - start)
        latest = None
        for line in tail.decode("utf-8", errors="replace").splitlines():
            event = parse_trae_log_event(line)
            if event is not None and event.kind == "model":
                latest = event
        return latest

    def _latest_correlated_model_from_tail(
        self,
        path: pathlib.Path,
        size: int,
    ) -> Optional[TraeLogEvent]:
        active_keys = {
            (task.session_id, task.task_id)
            for task in self._tasks
            if task.state != "completed"
        }
        if not active_keys:
            return None
        start = max(0, size - self._TASK_METADATA_RECOVERY_BYTES)
        with path.open("rb") as stream:
            stream.seek(start)
            tail = stream.read(size - start)
        if start > 0:
            first_newline = tail.find(b"\n")
            tail = tail[first_newline + 1 :] if first_newline >= 0 else b""
        latest = None
        for line in tail.decode("utf-8", errors="replace").splitlines():
            event = parse_trae_log_event(line)
            if event is None or event.kind != "model":
                continue
            if (event.session_id, event.task_id) in active_keys:
                latest = event
        return latest

    @staticmethod
    def _cursor_from_value(value: object) -> Optional[TraeLogCursor]:
        if not isinstance(value, dict):
            return None
        try:
            return TraeLogCursor(
                path=str(value["path"]),
                inode=int(value["inode"]),
                offset=int(value["offset"]),
                mtime_ns=int(value["mtime_ns"]),
                model=(
                    str(value.get("model", ""))[:64]
                    if isinstance(value.get("model", ""), str)
                    else ""
                ),
                effort=(
                    str(value.get("effort", ""))[:32]
                    if isinstance(value.get("effort", ""), str)
                    else ""
                ),
                model_updated_at=float(value.get("model_updated_at", 0.0)),
            )
        except (ValueError, TypeError, KeyError):
            return None

    def _load_cursors(self) -> Dict[str, TraeLogCursor]:
        try:
            value = json.loads(self._cursor_path.read_text(encoding="utf-8"))
        except (OSError, ValueError, TypeError, json.JSONDecodeError):
            return {}

        if isinstance(value, dict) and value.get("version") == 2:
            raw_cursors = value.get("cursors")
            if not isinstance(raw_cursors, dict):
                return {}
            cursors = {}
            for raw in raw_cursors.values():
                cursor = self._cursor_from_value(raw)
                if cursor is not None:
                    cursors[cursor.path] = cursor
            return cursors

        cursor = self._cursor_from_value(value)
        return {cursor.path: cursor} if cursor is not None else {}

    def _load_ledger(
        self,
    ) -> Tuple[
        Tuple[TraeTask, ...],
        Optional[str],
        Optional[str],
        float,
        int,
        bool,
    ]:
        try:
            value = json.loads(self._ledger_path.read_text(encoding="utf-8"))
            migrate_ledger = value.get("version") != self._LEDGER_VERSION
            tasks = []
            raw_tasks = (
                value.get("tasks", ())
                if not migrate_ledger
                else ()
            )
            for item in raw_tasks:
                state = item.get("state")
                if state not in {"pending", "running", "completed"}:
                    continue
                tasks.append(
                    TraeTask(
                        session_id=str(item["session_id"])[:128],
                        task_id=str(item["task_id"])[:128],
                        project=str(item.get("project", "--"))[:32],
                        title=str(item.get("title", "Trae task"))[:48],
                        detail=str(item.get("detail", ""))[:72],
                        state=state,
                        updated_at=float(item["updated_at"]),
                        model=(
                            str(item.get("model", ""))[:64]
                            if isinstance(item.get("model", ""), str)
                            else ""
                        ),
                        effort=(
                            str(item.get("effort", ""))[:32]
                            if isinstance(item.get("effort", ""), str)
                            else ""
                        ),
                    )
                )
            model = value.get("model")
            effort = value.get("effort")
            return (
                self._retain_ledger_tasks(tasks),
                model if isinstance(model, str) else None,
                effort if isinstance(effort, str) else None,
                float(value.get("model_updated_at", 0.0)),
                int(value.get("revision", 0)),
                migrate_ledger,
            )
        except (
            OSError,
            ValueError,
            TypeError,
            KeyError,
            json.JSONDecodeError,
        ):
            return (), None, None, 0.0, 0, False

    def _retain_ledger_tasks(
        self, tasks: Iterable[TraeTask]
    ) -> Tuple[TraeTask, ...]:
        def newest_first(item: TraeTask) -> Tuple[float, str, str]:
            return (-item.updated_at, item.session_id, item.task_id)

        active = sorted(
            (item for item in tasks if item.state != "completed"),
            key=newest_first,
        )
        completed = sorted(
            (item for item in tasks if item.state == "completed"),
            key=newest_first,
        )
        terminal_capacity = max(0, self._MAX_LEDGER_TASKS - len(active))
        return tuple(active + completed[:terminal_capacity])

    def _save_cursors(self) -> None:
        self._atomic_json_write(
            self._cursor_path,
            {
                "version": 2,
                "cursors": {
                    path: dataclasses.asdict(cursor)
                    for path, cursor in sorted(self._cursors.items())
                },
            },
        )

    def _save_ledger(self) -> None:
        self._atomic_json_write(
            self._ledger_path,
            {
                "version": self._LEDGER_VERSION,
                "model": self._model,
                "effort": self._effort,
                "model_updated_at": self._model_updated_at,
                "revision": self._revision,
                "tasks": [dataclasses.asdict(item) for item in self._tasks],
            },
        )

    @staticmethod
    def _atomic_json_write(path: pathlib.Path, value: object) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_name(path.name + ".tmp")
        temporary.write_text(
            json.dumps(value, ensure_ascii=False, sort_keys=True),
            encoding="utf-8",
        )
        os.replace(temporary, path)
