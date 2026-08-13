"""Fail-closed policy for route-bound watch voice submission."""

from __future__ import annotations

from task_routes import TaskRouteRegistry, TaskRouteTarget


class VoiceSubmitError(RuntimeError):
    """A route could not be safely submitted."""


class TaskVoiceSubmitter:
    def __init__(self, registry: TaskRouteRegistry, client) -> None:
        self._registry = registry
        self._client = client

    def snapshot(self, route: int) -> TaskRouteTarget:
        if isinstance(route, bool) or not isinstance(route, int) or not 1 <= route <= 0xFFFFFFFFFFFFFFFF:
            raise VoiceSubmitError("Task route is unavailable")
        target = self._registry.resolve(format(route, "016x"))
        if target is None or target.state not in ("pending", "running"):
            raise VoiceSubmitError("Task route is unavailable")
        if target.state == "running" and not target.expected_turn_id:
            raise VoiceSubmitError("Task route is unavailable")
        return target

    async def submit(self, target: TaskRouteTarget, text: str) -> None:
        current = self._registry.resolve(target.route)
        if current is None or current != target:
            raise VoiceSubmitError("Task route is stale")
        try:
            if target.state == "pending":
                await self._client.start_turn(target.thread_id, text)
            elif target.state == "running" and target.expected_turn_id:
                await self._client.steer_turn(
                    target.thread_id, target.expected_turn_id, text
                )
            else:
                raise VoiceSubmitError("Task route is unavailable")
        except VoiceSubmitError:
            raise
        except Exception as error:
            raise VoiceSubmitError("Task voice submission failed") from error
