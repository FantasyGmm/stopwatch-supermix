"""Parse and normalize the small Trae task surface sent to the watch."""

from __future__ import annotations

import dataclasses
import re
from typing import Iterable, Literal, Optional, Tuple


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
