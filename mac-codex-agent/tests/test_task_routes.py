import dataclasses
import pathlib
import re
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from task_routes import TaskRouteRegistry, TaskRouteTarget, route_id_for_thread


class RouteIDTests(unittest.TestCase):
    def test_route_id_is_stable_lowercase_64_bit_hex(self):
        route = route_id_for_thread("thread-a")

        self.assertEqual(route, "01e0a1b2900f0425")
        self.assertRegex(route, re.compile(r"^[0-9a-f]{16}$"))
        self.assertEqual(route_id_for_thread("thread-a"), route)

    def test_distinct_example_threads_have_distinct_routes(self):
        self.assertNotEqual(
            route_id_for_thread("thread-a"),
            route_id_for_thread("thread-b"),
        )

    def test_route_id_rejects_invalid_thread_ids(self):
        for value in ("", None, 7):
            with self.subTest(value=value), self.assertRaises(ValueError):
                route_id_for_thread(value)


class TaskRouteRegistryTests(unittest.TestCase):
    def target(self, **overrides):
        values = {
            "route": "01e0a1b2900f0425",
            "thread_id": "thread-a",
            "state": "running",
            "expected_turn_id": "turn-a",
            "revision": 4,
        }
        values.update(overrides)
        return TaskRouteTarget(**values)

    def test_target_is_immutable(self):
        target = self.target()

        with self.assertRaises(dataclasses.FrozenInstanceError):
            target.state = "completed"

    def test_update_and_resolve_valid_target(self):
        target = self.target()
        registry = TaskRouteRegistry()

        registry.update([target])

        self.assertEqual(registry.resolve(target.route), target)

    def test_collision_invalidates_every_target_on_that_route(self):
        route = "01e0a1b2900f0425"
        safe = self.target(
            route="dec63af850e04cd7",
            thread_id="thread-b",
            expected_turn_id="turn-safe",
        )
        registry = TaskRouteRegistry(
            route_deriver=lambda thread_id: (
                route
                if thread_id in ("thread-a", "thread-collision")
                else safe.route
            )
        )

        registry.update(
            [
                self.target(route=route),
                self.target(
                    route=route,
                    thread_id="thread-collision",
                    expected_turn_id="turn-collision",
                ),
                safe,
            ]
        )

        self.assertIsNone(registry.resolve(route))
        self.assertEqual(registry.resolve(safe.route), safe)

    def test_rejects_route_that_does_not_match_thread(self):
        with self.assertRaises(ValueError):
            TaskRouteRegistry().update(
                [
                    self.target(
                        route="dec63af850e04cd7",
                        thread_id="thread-a",
                    )
                ]
            )

    def test_collision_remains_tombstoned_after_one_target_is_removed(self):
        route = "01e0a1b2900f0425"
        first = self.target(route=route)
        second = self.target(
            route=route,
            thread_id="thread-collision",
            expected_turn_id="turn-collision",
        )
        registry = TaskRouteRegistry(route_deriver=lambda _thread_id: route)

        registry.update([first, second])
        registry.update([first])

        self.assertIsNone(registry.resolve(route))

    def test_normal_removal_does_not_tombstone_route(self):
        target = self.target()
        registry = TaskRouteRegistry()

        registry.update([target])
        registry.update([])
        registry.update([target])

        self.assertEqual(registry.resolve(target.route), target)

    def test_explicit_reset_clears_collision_tombstone(self):
        route = "01e0a1b2900f0425"
        target = self.target(route=route)
        collision = self.target(
            route=route,
            thread_id="thread-collision",
            expected_turn_id="turn-collision",
        )
        registry = TaskRouteRegistry(route_deriver=lambda _thread_id: route)
        registry.update([target, collision])

        registry.reset()
        registry.update([target])

        self.assertEqual(registry.resolve(route), target)

    def test_running_target_requires_expected_turn(self):
        registry = TaskRouteRegistry()

        for turn_id in (None, ""):
            with self.subTest(turn_id=turn_id), self.assertRaises(ValueError):
                registry.update([self.target(expected_turn_id=turn_id)])

    def test_completed_target_can_omit_expected_turn(self):
        target = self.target(state="completed", expected_turn_id=None)
        registry = TaskRouteRegistry()

        registry.update([target])

        self.assertEqual(registry.resolve(target.route), target)

    def test_nonrunning_target_rejects_nonstring_expected_turn(self):
        for state in ("pending", "completed"):
            with self.subTest(state=state), self.assertRaises(ValueError):
                TaskRouteRegistry().update(
                    [self.target(state=state, expected_turn_id=7)]
                )

    def test_revision_rejects_bool(self):
        with self.assertRaises(ValueError):
            TaskRouteRegistry().update([self.target(revision=True)])

    def test_rejects_invalid_state(self):
        with self.assertRaises(ValueError):
            TaskRouteRegistry().update([self.target(state="unknown")])

    def test_rejects_invalid_routes_on_update_and_resolve(self):
        registry = TaskRouteRegistry()

        for route in ("", "ABCDEF0123456789", "0123", None):
            with self.subTest(operation="update", route=route), self.assertRaises(
                ValueError
            ):
                registry.update([self.target(route=route)])
            with self.subTest(operation="resolve", route=route), self.assertRaises(
                ValueError
            ):
                registry.resolve(route)


if __name__ == "__main__":
    unittest.main()
