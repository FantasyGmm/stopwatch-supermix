import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from task_routes import TaskRouteRegistry, TaskRouteTarget, route_id_for_thread
from task_voice_submitter import TaskVoiceSubmitter, VoiceSubmitError


class FakeClient:
    def __init__(self):
        self.calls = []

    async def start_turn(self, thread_id, text):
        self.calls.append(("start", thread_id, text))

    async def steer_turn(self, thread_id, expected_turn_id, text):
        self.calls.append(("steer", thread_id, expected_turn_id, text))


class TaskVoiceSubmitterTests(unittest.IsolatedAsyncioTestCase):
    async def test_pending_starts_and_running_steers_captured_target(self):
        registry = TaskRouteRegistry()
        pending = TaskRouteTarget(route_id_for_thread("p"), "p", "pending", None, 4)
        running = TaskRouteTarget(route_id_for_thread("r"), "r", "running", "turn-r", 4)
        registry.update((pending, running))
        client = FakeClient()
        submitter = TaskVoiceSubmitter(registry, client)

        pending_snapshot = submitter.snapshot(int(pending.route, 16))
        running_snapshot = submitter.snapshot(int(running.route, 16))
        await submitter.submit(pending_snapshot, "one")
        await submitter.submit(running_snapshot, "two")

        self.assertEqual(client.calls, [("start", "p", "one"), ("steer", "r", "turn-r", "two")])

    async def test_revision_refresh_does_not_invalidate_the_same_pending_target(self):
        registry = TaskRouteRegistry()
        route = route_id_for_thread("p")
        captured = TaskRouteTarget(route, "p", "pending", None, 4)
        registry.update((captured,))
        client = FakeClient()
        submitter = TaskVoiceSubmitter(registry, client)
        snapshot = submitter.snapshot(int(route, 16))
        registry.update((TaskRouteTarget(route, "p", "pending", None, 5),))

        await submitter.submit(snapshot, "next")

        self.assertEqual(client.calls, [("start", "p", "next")])

    async def test_running_task_that_finishes_during_recording_starts_next_turn(self):
        registry = TaskRouteRegistry()
        route = route_id_for_thread("r")
        running = TaskRouteTarget(route, "r", "running", "turn-r", 4)
        registry.update((running,))
        client = FakeClient()
        submitter = TaskVoiceSubmitter(registry, client)
        snapshot = submitter.snapshot(int(route, 16))
        registry.update((TaskRouteTarget(route, "r", "completed", None, 5),))

        await submitter.submit(snapshot, "continue")

        self.assertEqual(client.calls, [("start", "r", "continue")])

    async def test_completed_detail_can_start_the_next_turn(self):
        registry = TaskRouteRegistry()
        route = route_id_for_thread("completed-detail")
        completed = TaskRouteTarget(
            route, "completed-detail", "completed", None, 6
        )
        registry.update((completed,))
        client = FakeClient()
        submitter = TaskVoiceSubmitter(registry, client)

        snapshot = submitter.snapshot(int(route, 16))
        await submitter.submit(snapshot, "继续处理")

        self.assertEqual(
            client.calls,
            [("start", "completed-detail", "继续处理")],
        )

    async def test_changed_running_turn_still_fails_closed(self):
        registry = TaskRouteRegistry()
        route = route_id_for_thread("r")
        running = TaskRouteTarget(route, "r", "running", "turn-old", 4)
        registry.update((running,))
        client = FakeClient()
        submitter = TaskVoiceSubmitter(registry, client)
        snapshot = submitter.snapshot(int(route, 16))
        registry.update((TaskRouteTarget(route, "r", "running", "turn-new", 5),))

        with self.assertRaises(VoiceSubmitError):
            await submitter.submit(snapshot, "must not redirect")

        self.assertEqual(client.calls, [])

    async def test_missing_stale_collision_and_missing_turn_fail_closed(self):
        registry = TaskRouteRegistry()
        target = TaskRouteTarget(route_id_for_thread("a"), "a", "pending", None, 1)
        registry.update((target,))
        client = FakeClient()
        submitter = TaskVoiceSubmitter(registry, client)
        snapshot = submitter.snapshot(int(target.route, 16))
        registry.update(())
        with self.assertRaises(VoiceSubmitError):
            await submitter.submit(snapshot, "stale")
        with self.assertRaises(VoiceSubmitError):
            submitter.snapshot(0xFFFFFFFFFFFFFFFF)

        collision_registry = TaskRouteRegistry(route_deriver=lambda _thread: "1111111111111111")
        collision_registry.update(
            (
                TaskRouteTarget("1111111111111111", "x", "pending", None, 1),
                TaskRouteTarget("1111111111111111", "y", "pending", None, 1),
            )
        )
        with self.assertRaises(VoiceSubmitError):
            TaskVoiceSubmitter(collision_registry, client).snapshot(0x1111111111111111)
        self.assertEqual(client.calls, [])


if __name__ == "__main__":
    unittest.main()
