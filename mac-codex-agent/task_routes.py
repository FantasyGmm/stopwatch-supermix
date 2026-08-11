"""Opaque, collision-safe routing for Codex task actions."""

from __future__ import annotations

import dataclasses
import hashlib
import re
import threading
from typing import Callable, Dict, Iterable, Optional, Set


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


def _validate_target(
    target: TaskRouteTarget,
    route_deriver: Callable[[str], str],
) -> None:
    if not isinstance(target, TaskRouteTarget):
        raise ValueError("target must be a TaskRouteTarget")
    _validate_route(target.route)
    if not isinstance(target.thread_id, str) or not target.thread_id:
        raise ValueError("thread_id must be a nonempty string")
    expected_route = route_deriver(target.thread_id)
    _validate_route(expected_route)
    if target.route != expected_route:
        raise ValueError("route does not match thread_id")
    if target.state not in _VALID_STATES:
        raise ValueError("invalid task route state")
    if target.expected_turn_id is not None and not isinstance(
        target.expected_turn_id, str
    ):
        raise ValueError("expected_turn_id must be a string or None")
    if target.state == "running" and (
        not isinstance(target.expected_turn_id, str) or not target.expected_turn_id
    ):
        raise ValueError("running targets require an expected turn ID")
    if (
        isinstance(target.revision, bool)
        or not isinstance(target.revision, int)
        or target.revision < 0
    ):
        raise ValueError("revision must be a nonnegative integer")


class TaskRouteRegistry:
    """An atomic route view that fails closed when route IDs collide."""

    def __init__(
        self,
        route_deriver: Callable[[str], str] = route_id_for_thread,
    ) -> None:
        if not callable(route_deriver):
            raise ValueError("route_deriver must be callable")
        self._route_deriver = route_deriver
        self._lock = threading.RLock()
        self._targets: Dict[str, TaskRouteTarget] = {}
        self._collided_routes: Set[str] = set()

    def update(self, targets: Iterable[TaskRouteTarget]) -> None:
        grouped = {}
        for target in targets:
            _validate_target(target, self._route_deriver)
            grouped.setdefault(target.route, []).append(target)
        collisions = {
            route
            for route, route_targets in grouped.items()
            if len(route_targets) > 1
        }
        with self._lock:
            self._collided_routes.update(collisions)
            self._targets = {
                route: route_targets[0]
                for route, route_targets in grouped.items()
                if len(route_targets) == 1
                and route not in self._collided_routes
            }

    def resolve(self, route: str) -> Optional[TaskRouteTarget]:
        _validate_route(route)
        with self._lock:
            return self._targets.get(route)

    def reset(self) -> None:
        with self._lock:
            self._targets = {}
            self._collided_routes = set()
