"""Parse and normalize the small Trae task surface sent to the watch."""

from __future__ import annotations

import dataclasses
import json
import os
import pathlib
import re
from typing import Iterable, Literal, Optional, Sequence, Tuple


_IDENTIFIER = r"[A-Za-z0-9_.:-]{1,128}"
_SESSION_RE = re.compile(r"\bsession_id=(?P<value>{})\b".format(_IDENTIFIER))
_TASK_RE = re.compile(r"\btask_id=(?P<value>{})\b".format(_IDENTIFIER))
_MODEL_RE = re.compile(
    r'model_info:\s*CustomModel\s*\{[^}\r\n]{0,512}?'
    r'display_model_name:\s*Some\("(?P<model>[^"\r\n]{1,64})"\)'
    r'[^}\r\n]{0,512}?reasoning_effort:\s*'
    r'(?:(?:Some\("(?P<effort>[^"\r\n]{1,32})"\))|None)'
    r"[^}\r\n]{0,512}?\}"
)


@dataclasses.dataclass(frozen=True)
class TraeLogEvent:
    kind: Literal["started", "progress", "pending", "completed", "model"]
    session_id: Optional[str] = None
    task_id: Optional[str] = None
    model: Optional[str] = None
    effort: Optional[str] = None


@dataclasses.dataclass(frozen=True)
class TraeTask:
    session_id: str
    task_id: str
    project: str
    title: str
    detail: str
    state: Literal["pending", "running", "completed"]
    updated_at: float


@dataclasses.dataclass(frozen=True)
class TraeLogCursor:
    path: str
    inode: int
    offset: int
    mtime_ns: int


@dataclasses.dataclass(frozen=True)
class TraeTaskSnapshot:
    tasks: Tuple[TraeTask, ...]
    model: Optional[str]
    effort: Optional[str]
    revision: int


def parse_trae_log_event(line: str) -> Optional[TraeLogEvent]:
    if not isinstance(line, str) or not line:
        return None

    model_match = _MODEL_RE.search(line)
    if model_match is not None:
        return TraeLogEvent(
            kind="model",
            model=model_match.group("model"),
            effort=model_match.group("effort"),
        )

    kind = None
    if "execute_task:start" in line:
        kind = "started"
    elif "[append_resume_listener] started" in line:
        kind = "progress"
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
    )


def apply_trae_log_event(
    tasks: Iterable[TraeTask],
    event: TraeLogEvent,
    now: float,
) -> Tuple[TraeTask, ...]:
    current = list(tasks)
    if event.kind == "model" or event.session_id is None or event.task_id is None:
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
    )
    if matching_index is None:
        current.append(replacement)
    else:
        current[matching_index] = replacement
    return tuple(current)


def select_transport_tasks(
    tasks: Iterable[TraeTask],
    limit: int = 4,
) -> Tuple[TraeTask, ...]:
    state_order = {"pending": 0, "running": 1, "completed": 2}
    selected = sorted(
        tasks,
        key=lambda item: (
            state_order[item.state],
            -item.updated_at,
            item.session_id,
            item.task_id,
        ),
    )
    return tuple(selected[: max(0, limit)])


class TraeTaskTracker:
    _METADATA_TAIL_BYTES = 256 * 1024
    _MAX_LEDGER_TASKS = 32

    def __init__(self, cursor_path: pathlib.Path, ledger_path: pathlib.Path):
        self._cursor_path = pathlib.Path(cursor_path)
        self._ledger_path = pathlib.Path(ledger_path)
        self._cursor = self._load_cursor()
        self._tasks, self._model, self._effort, self._revision = self._load_ledger()

    def refresh(
        self,
        log_paths: Sequence[pathlib.Path],
        now: float,
    ) -> TraeTaskSnapshot:
        path = self._select_log(log_paths)
        if path is None:
            return self._snapshot()

        stat = path.stat()
        requires_baseline = (
            self._cursor is None
            or self._cursor.path != str(path)
            or self._cursor.inode != stat.st_ino
            or stat.st_size < self._cursor.offset
        )
        if requires_baseline:
            self._baseline(path, stat)
            return self._snapshot()

        with path.open("rb") as stream:
            stream.seek(self._cursor.offset)
            appended = stream.read()
        complete_length = appended.rfind(b"\n") + 1
        changed = False
        if complete_length > 0:
            for line in appended[:complete_length].decode(
                "utf-8", errors="replace"
            ).splitlines():
                event = parse_trae_log_event(line)
                if event is None:
                    continue
                if event.kind == "model":
                    next_pair = (event.model, event.effort)
                    if next_pair != (self._model, self._effort):
                        self._model, self._effort = next_pair
                        changed = True
                    continue
                updated = apply_trae_log_event(self._tasks, event, now)
                if updated != self._tasks:
                    self._tasks = updated
                    changed = True

        self._cursor = TraeLogCursor(
            path=str(path),
            inode=stat.st_ino,
            offset=self._cursor.offset + complete_length,
            mtime_ns=stat.st_mtime_ns,
        )
        if changed:
            self._revision += 1
            self._tasks = tuple(
                sorted(
                    self._tasks,
                    key=lambda item: (
                        -item.updated_at,
                        item.session_id,
                        item.task_id,
                    ),
                )[: self._MAX_LEDGER_TASKS]
            )
            self._save_ledger()
        self._save_cursor()
        return self._snapshot()

    def _snapshot(self) -> TraeTaskSnapshot:
        return TraeTaskSnapshot(
            tasks=select_transport_tasks(self._tasks),
            model=self._model,
            effort=self._effort,
            revision=self._revision,
        )

    @staticmethod
    def _select_log(
        log_paths: Sequence[pathlib.Path],
    ) -> Optional[pathlib.Path]:
        candidates = [
            pathlib.Path(path)
            for path in log_paths
            if pathlib.Path(path).is_file()
        ]
        if not candidates:
            return None
        return max(
            candidates,
            key=lambda path: (path.stat().st_mtime_ns, str(path)),
        )

    def _baseline(self, path: pathlib.Path, stat: os.stat_result) -> None:
        model = self._latest_model_from_tail(path, stat.st_size)
        changed = model is not None and model != (self._model, self._effort)
        if model is not None:
            self._model, self._effort = model
        self._cursor = TraeLogCursor(
            path=str(path),
            inode=stat.st_ino,
            offset=stat.st_size,
            mtime_ns=stat.st_mtime_ns,
        )
        if changed:
            self._revision += 1
        self._save_cursor()
        self._save_ledger()

    def _latest_model_from_tail(
        self,
        path: pathlib.Path,
        size: int,
    ) -> Optional[Tuple[Optional[str], Optional[str]]]:
        start = max(0, size - self._METADATA_TAIL_BYTES)
        with path.open("rb") as stream:
            stream.seek(start)
            tail = stream.read(self._METADATA_TAIL_BYTES)
        latest = None
        for line in tail.decode("utf-8", errors="replace").splitlines():
            event = parse_trae_log_event(line)
            if event is not None and event.kind == "model":
                latest = (event.model, event.effort)
        return latest

    def _load_cursor(self) -> Optional[TraeLogCursor]:
        try:
            value = json.loads(self._cursor_path.read_text(encoding="utf-8"))
            return TraeLogCursor(
                path=str(value["path"]),
                inode=int(value["inode"]),
                offset=int(value["offset"]),
                mtime_ns=int(value["mtime_ns"]),
            )
        except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError):
            return None

    def _load_ledger(
        self,
    ) -> Tuple[Tuple[TraeTask, ...], Optional[str], Optional[str], int]:
        try:
            value = json.loads(self._ledger_path.read_text(encoding="utf-8"))
            tasks = []
            for item in value.get("tasks", ()):
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
                    )
                )
            model = value.get("model")
            effort = value.get("effort")
            return (
                tuple(tasks[: self._MAX_LEDGER_TASKS]),
                model if isinstance(model, str) else None,
                effort if isinstance(effort, str) else None,
                int(value.get("revision", 0)),
            )
        except (
            OSError,
            ValueError,
            TypeError,
            KeyError,
            json.JSONDecodeError,
        ):
            return (), None, None, 0

    def _save_cursor(self) -> None:
        if self._cursor is None:
            return
        self._atomic_json_write(
            self._cursor_path,
            dataclasses.asdict(self._cursor),
        )

    def _save_ledger(self) -> None:
        self._atomic_json_write(
            self._ledger_path,
            {
                "version": 1,
                "model": self._model,
                "effort": self._effort,
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
