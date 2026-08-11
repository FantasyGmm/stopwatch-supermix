"""Opaque, collision-safe routing for Codex task actions."""

from __future__ import annotations

import dataclasses
import hashlib
import re
from typing import Dict, Iterable, Optional


_ROUTE_PATTERN = re.compile(r"^[0-9a-f]{16}$")
_VALID_STATES = frozenset(("running", "pending", "completed"))


def route_id_for_thread(thread_id: str) -> str:
    if not isinstance(thread_id, str) or not thread_id:
        raise ValueError("thread_id must be a nonempty string")
    return hashlib.blake2b(
        thread_id.encode("utf-8"),
        digest_size=8,
        person=b"SWRoute1",
    ).hexdigest()


@dataclasses.dataclass(frozen=True)
class TaskRouteTarget:
    route: str
    thread_id: str
    state: str
    expected_turn_id: Optional[str]
    revision: int


def _validate_route(route: str) -> None:
    if not isinstance(route, str) or _ROUTE_PATTERN.fullmatch(route) is None:
        raise ValueError("route must be exactly 16 lowercase hex characters")


def _validate_target(target: TaskRouteTarget) -> None:
    if not isinstance(target, TaskRouteTarget):
        raise ValueError("target must be a TaskRouteTarget")
    _validate_route(target.route)
    if not isinstance(target.thread_id, str) or not target.thread_id:
        raise ValueError("thread_id must be a nonempty string")
    if target.state not in _VALID_STATES:
        raise ValueError("invalid task route state")
    if target.state == "running" and (
        not isinstance(target.expected_turn_id, str) or not target.expected_turn_id
    ):
        raise ValueError("running targets require an expected turn ID")
    if not isinstance(target.revision, int) or target.revision < 0:
        raise ValueError("revision must be a nonnegative integer")


class TaskRouteRegistry:
    """An atomic route view that fails closed when route IDs collide."""

    def __init__(self) -> None:
        self._targets: Dict[str, TaskRouteTarget] = {}

    def update(self, targets: Iterable[TaskRouteTarget]) -> None:
        grouped = {}
        for target in targets:
            _validate_target(target)
            grouped.setdefault(target.route, []).append(target)
        self._targets = {
            route: route_targets[0]
            for route, route_targets in grouped.items()
            if len(route_targets) == 1
        }

    def resolve(self, route: str) -> Optional[TaskRouteTarget]:
        _validate_route(route)
        return self._targets.get(route)
