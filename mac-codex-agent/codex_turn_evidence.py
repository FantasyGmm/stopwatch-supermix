"""Reduce Codex turn signals and replay terminal lifecycle evidence."""

from __future__ import annotations

import dataclasses
import datetime
import json
import pathlib
from typing import Any, Dict, Optional


UTC = datetime.timezone.utc


class RolloutReplayError(RuntimeError):
    def __init__(self, message: str, *, retryable: bool = False):
        super().__init__(message)
        self.retryable = retryable


@dataclasses.dataclass(frozen=True)
class LatestCompletion:
    timestamp: datetime.datetime
    turn_id: str
    last_agent_message: Any
    unfinished_plan: bool
    unresolved_input: bool


@dataclasses.dataclass(frozen=True)
class MatchingTurnAbort:
    timestamp: datetime.datetime
    turn_id: str


def new_turn_signal(turn_id: str) -> Dict[str, Any]:
    return {
        "turn_id": turn_id,
        "unfinished_plan": False,
        "input_calls": [],
    }


def reduce_turn_signal(signal: Dict[str, Any], payload: Dict[str, Any]) -> bool:
    item_type = payload.get("type")
    if item_type == "function_call" and payload.get("name") == "update_plan":
        arguments = payload.get("arguments")
        if isinstance(arguments, str):
            try:
                arguments = json.loads(arguments)
            except json.JSONDecodeError:
                return False
        if not isinstance(arguments, dict):
            return False
        plan = arguments.get("plan")
        if not isinstance(plan, list):
            return False
        unfinished = any(
            isinstance(item, dict)
            and item.get("status") in ("in_progress", "pending")
            for item in plan
        )
        if signal.get("unfinished_plan") == unfinished:
            return False
        signal["unfinished_plan"] = unfinished
        return True

    if item_type == "function_call" and payload.get("name") == "request_user_input":
        call_id = payload.get("call_id")
        if not isinstance(call_id, str) or not call_id:
            return False
        calls = signal.setdefault("input_calls", [])
        if call_id in calls:
            return False
        calls.append(call_id)
        return True

    if item_type == "function_call_output":
        call_id = payload.get("call_id")
        calls = signal.setdefault("input_calls", [])
        if call_id not in calls:
            return False
        calls.remove(call_id)
        return True
    return False


def _timestamp(value: Any) -> datetime.datetime:
    if not isinstance(value, str) or not value:
        raise RolloutReplayError("rollout event has no timestamp")
    try:
        parsed = datetime.datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise RolloutReplayError("rollout event has invalid timestamp") from error
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=UTC)
    return parsed.astimezone(UTC)


def matching_turn_abort(
    path: pathlib.Path,
    started_at: datetime.datetime,
) -> Optional[MatchingTurnAbort]:
    if started_at.tzinfo is None:
        started_at = started_at.replace(tzinfo=UTC)
    started_at = started_at.astimezone(UTC)
    try:
        stream = pathlib.Path(path).open("rb")
    except OSError as error:
        raise RolloutReplayError("rollout is unreadable", retryable=True) from error

    turn_id = None
    try:
        for raw in stream:
            if not raw.endswith(b"\n"):
                raise RolloutReplayError(
                    "rollout has an incomplete trailing record",
                    retryable=True,
                )
            try:
                item = json.loads(raw)
            except (UnicodeDecodeError, json.JSONDecodeError) as error:
                raise RolloutReplayError(
                    "rollout contains an invalid record"
                ) from error
            if not isinstance(item, dict) or item.get("type") != "event_msg":
                continue
            payload = item.get("payload")
            if not isinstance(payload, dict):
                continue
            event_type = payload.get("type")
            if event_type == "task_started" and _timestamp(
                item.get("timestamp") or payload.get("timestamp")
            ) == started_at:
                candidate = payload.get("turn_id")
                if isinstance(candidate, str) and candidate:
                    turn_id = candidate
                continue
            if (
                turn_id is None
                or event_type != "turn_aborted"
                or payload.get("turn_id") != turn_id
            ):
                continue
            return MatchingTurnAbort(
                timestamp=_timestamp(
                    item.get("timestamp") or payload.get("timestamp")
                ),
                turn_id=turn_id,
            )
    except OSError as error:
        raise RolloutReplayError("rollout is unreadable", retryable=True) from error
    finally:
        stream.close()
    return None


def latest_unsuperseded_completion(
    path: pathlib.Path,
) -> Optional[LatestCompletion]:
    try:
        stream = pathlib.Path(path).open("rb")
    except OSError as error:
        raise RolloutReplayError("rollout is unreadable", retryable=True) from error

    signal = None
    candidate = None
    try:
        for raw in stream:
            if not raw.endswith(b"\n"):
                raise RolloutReplayError(
                    "rollout has an incomplete trailing record",
                    retryable=True,
                )
            try:
                item = json.loads(raw)
            except (UnicodeDecodeError, json.JSONDecodeError) as error:
                raise RolloutReplayError(
                    "rollout contains an invalid record"
                ) from error
            if not isinstance(item, dict) or not isinstance(
                item.get("payload"), dict
            ):
                continue
            payload = item["payload"]

            if item.get("type") == "response_item":
                metadata = payload.get(
                    "internal_chat_message_metadata_passthrough"
                )
                turn_id = (
                    metadata.get("turn_id") if isinstance(metadata, dict) else None
                )
                if signal is not None and (
                    not isinstance(turn_id, str)
                    or not turn_id
                    or turn_id == signal.get("turn_id")
                ):
                    reduce_turn_signal(signal, payload)
                continue
            if item.get("type") != "event_msg":
                continue

            event_type = payload.get("type")
            if event_type == "task_started":
                turn_id = payload.get("turn_id")
                signal = (
                    new_turn_signal(turn_id)
                    if isinstance(turn_id, str) and turn_id
                    else None
                )
                candidate = None
            elif event_type == "user_message":
                candidate = None
            elif event_type == "task_complete":
                turn_id = payload.get("turn_id")
                matching = (
                    signal
                    if signal is not None and signal.get("turn_id") == turn_id
                    else {}
                )
                candidate = LatestCompletion(
                    timestamp=_timestamp(
                        item.get("timestamp") or payload.get("timestamp")
                    ),
                    turn_id=turn_id if isinstance(turn_id, str) else "",
                    last_agent_message=payload.get("last_agent_message"),
                    unfinished_plan=bool(
                        matching.get("unfinished_plan", False)
                    ),
                    unresolved_input=bool(matching.get("input_calls")),
                )
                signal = None
    except OSError as error:
        raise RolloutReplayError("rollout is unreadable") from error
    finally:
        stream.close()
    return candidate
