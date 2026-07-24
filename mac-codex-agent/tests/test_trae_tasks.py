import dataclasses
import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from trae_tasks import (
    TraeTask,
    apply_trae_log_event,
    parse_trae_log_event,
    select_transport_tasks,
)


FIXTURE = pathlib.Path(__file__).parent / "fixtures" / "trae" / "agent.log"


def task(task_id, state, updated_at):
    return TraeTask(
        session_id="session-{}".format(task_id),
        task_id=task_id,
        project="StopWatch",
        title="Task {}".format(task_id),
        detail="Sanitized detail",
        state=state,
        updated_at=updated_at,
    )


class TraeTaskTests(unittest.TestCase):
    def test_parser_maps_start_progress_pending_and_exit(self):
        events = [
            parse_trae_log_event(line)
            for line in FIXTURE.read_text(encoding="utf-8").splitlines()
        ]

        self.assertEqual(
            [event.kind for event in events],
            ["model", "started", "progress", "progress", "pending", "completed"],
        )
        self.assertEqual(events[0].model, "Model A")
        self.assertEqual(events[0].effort, "high")
        self.assertEqual(events[1].session_id, "session-a")
        self.assertEqual(events[1].task_id, "task-a")

    def test_generic_listener_exit_does_not_clear_an_existing_pending_state(self):
        pending = task("a", "pending", 1.0)
        exit_event = parse_trae_log_event(
            "[append_resume_listener] exiting "
            "session_id=session-a task_id=a"
        )

        updated = apply_trae_log_event((pending,), exit_event, now=2.0)

        self.assertEqual(updated, (pending,))

    def test_new_execution_after_pending_returns_the_task_to_running(self):
        pending = task("a", "pending", 1.0)
        start_event = parse_trae_log_event(
            "execute_task:start session_id=session-a task_id=a"
        )

        updated = apply_trae_log_event((pending,), start_event, now=2.0)

        self.assertEqual(len(updated), 1)
        self.assertEqual(updated[0].state, "running")
        self.assertEqual(updated[0].updated_at, 2.0)

    def test_unknown_or_malformed_lines_produce_no_event(self):
        cases = (
            "",
            "ordinary diagnostic output",
            "execute_task:start session_id=session-a",
            "ToolCall arrived name=AskUserQuestion task_id=task-a",
            "model_info: CustomModel { display_model_name: None }",
        )

        for line in cases:
            with self.subTest(line=line):
                self.assertIsNone(parse_trae_log_event(line))

    def test_parser_result_never_contains_raw_line(self):
        line = (
            "execute_task:start session_id=session-a task_id=task-a "
            "prompt=do-not-retain-this"
        )

        event = parse_trae_log_event(line)

        self.assertIsNotNone(event)
        self.assertNotIn("raw", {field.name for field in dataclasses.fields(event)})
        self.assertNotIn("do-not-retain-this", repr(event))

    def test_priority_is_pending_then_running_then_latest_completed(self):
        selected = select_transport_tasks(
            (
                task("completed-old", "completed", 1.0),
                task("running", "running", 3.0),
                task("pending", "pending", 2.0),
                task("completed-new", "completed", 4.0),
            )
        )

        self.assertEqual(
            [item.task_id for item in selected],
            ["pending", "running", "completed-new", "completed-old"],
        )
        self.assertEqual(
            {item.state for item in selected},
            {"pending", "running", "completed"},
        )

    def test_transport_selection_is_stable_and_limited_to_four(self):
        tasks = tuple(task(str(index), "running", 5.0) for index in range(8))

        first = select_transport_tasks(reversed(tasks), limit=4)
        second = select_transport_tasks(tasks, limit=4)

        self.assertEqual(first, second)
        self.assertEqual([item.task_id for item in first], ["0", "1", "2", "3"])


if __name__ == "__main__":
    unittest.main()
