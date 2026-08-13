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
