import dataclasses
import json
import pathlib
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from trae_tasks import (
    TraeTask,
    TraeTaskTracker,
    apply_trae_log_event,
    ordered_active_tasks,
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

    def test_ordered_active_tasks_excludes_terminal_and_is_stable(self):
        tasks = (
            TraeTask("s3", "t3", "P3", "Done", "", "completed", 30.0),
            TraeTask("s2", "t2", "P2", "Run", "", "running", 20.0),
            TraeTask("s1", "t1", "P1", "Ask", "", "pending", 10.0),
            TraeTask("s0", "t0", "P0", "Ask newer", "", "pending", 40.0),
        )
        ordered = ordered_active_tasks(tasks)
        self.assertEqual([item.task_id for item in ordered], ["t0", "t1", "t2"])

    def test_transport_selection_is_stable_and_limited_to_four(self):
        tasks = tuple(task(str(index), "running", 5.0) for index in range(8))

        first = select_transport_tasks(reversed(tasks), limit=4)
        second = select_transport_tasks(tasks, limit=4)

        self.assertEqual(first, second)
        self.assertEqual([item.task_id for item in first], ["0", "1", "2", "3"])


class TraeTaskTrackerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name)
        self.log = self.root / "agent.log"
        self.cursor = self.root / "cursor.json"
        self.ledger = self.root / "ledger.json"

    def tearDown(self):
        self.temporary.cleanup()

    def tracker(self):
        return TraeTaskTracker(
            cursor_path=self.cursor,
            ledger_path=self.ledger,
        )

    def append(self, *lines, complete=True):
        with self.log.open("ab") as stream:
            for index, line in enumerate(lines):
                stream.write(line.encode("utf-8"))
                if complete or index < len(lines) - 1:
                    stream.write(b"\n")

    def test_first_observation_baselines_at_eof_without_replaying_history(self):
        self.append(
            "execute_task:start session_id=session-old task_id=task-old",
            "ToolCall arrived name=AskUserQuestion "
            "session_id=session-old task_id=task-old",
        )

        snapshot = self.tracker().refresh((self.log,), now=10.0)

        self.assertEqual(snapshot.tasks, ())
        saved = json.loads(self.cursor.read_text(encoding="utf-8"))
        self.assertEqual(saved["version"], 2)
        cursor = saved["cursors"][str(self.log)]
        self.assertEqual(cursor["path"], str(self.log))
        self.assertEqual(cursor["offset"], self.log.stat().st_size)
        self.assertEqual(cursor["inode"], self.log.stat().st_ino)
        self.assertEqual(cursor["mtime_ns"], self.log.stat().st_mtime_ns)

    def test_first_observation_scans_only_a_bounded_tail_for_latest_model_metadata(self):
        old = (
            'model_info: CustomModel { config_name: "old", '
            'display_model_name: Some("Old Model"), '
            'reasoning_effort: Some("low") }\n'
        )
        newest = (
            'model_info: CustomModel { config_name: "new", '
            'display_model_name: Some("New Model"), '
            'reasoning_effort: Some("high") }\n'
        )
        self.log.write_bytes(
            old.encode("utf-8")
            + (b"x" * (257 * 1024))
            + b"\nexecute_task:start session_id=session-old task_id=task-old\n"
            + newest.encode("utf-8")
        )

        snapshot = self.tracker().refresh((self.log,), now=10.0)

        self.assertEqual(snapshot.model, "New Model")
        self.assertEqual(snapshot.effort, "high")
        self.assertEqual(snapshot.tasks, ())

    def test_refresh_reads_only_bytes_appended_after_cursor(self):
        self.log.touch()
        tracker = self.tracker()
        baseline = tracker.refresh((self.log,), now=10.0)
        self.append(
            "execute_task:start session_id=session-a task_id=task-a",
        )

        updated = tracker.refresh((self.log,), now=11.0)

        self.assertEqual(baseline.tasks, ())
        self.assertEqual(len(updated.tasks), 1)
        self.assertEqual(updated.tasks[0].task_id, "task-a")
        self.assertEqual(updated.tasks[0].state, "running")
        self.assertEqual(
            json.loads(self.cursor.read_text(encoding="utf-8"))["cursors"][
                str(self.log)
            ]["offset"],
            self.log.stat().st_size,
        )

    def test_cursor_survives_tracker_recreation(self):
        self.log.touch()
        self.tracker().refresh((self.log,), now=10.0)
        self.append(
            "execute_task:start session_id=session-a task_id=task-a",
        )
        first = self.tracker().refresh((self.log,), now=11.0)
        self.append(
            "ToolCall arrived name=AskUserQuestion "
            "session_id=session-a task_id=task-a",
        )

        second = self.tracker().refresh((self.log,), now=12.0)

        self.assertEqual(first.tasks[0].state, "running")
        self.assertEqual(second.tasks[0].state, "pending")
        self.assertGreater(second.revision, first.revision)

    def test_legacy_single_cursor_is_migrated_without_replaying_history(self):
        self.log.write_text("baseline\n", encoding="utf-8")
        stat = self.log.stat()
        self.cursor.write_text(
            json.dumps(
                {
                    "path": str(self.log),
                    "inode": stat.st_ino,
                    "offset": stat.st_size,
                    "mtime_ns": stat.st_mtime_ns,
                }
            ),
            encoding="utf-8",
        )
        tracker = self.tracker()
        self.append("execute_task:start session_id=session-a task_id=task-a")

        snapshot = tracker.refresh((self.log,), now=11.0)

        self.assertEqual([item.task_id for item in snapshot.tasks], ["task-a"])
        saved = json.loads(self.cursor.read_text(encoding="utf-8"))
        self.assertEqual(saved["version"], 2)
        self.assertEqual(set(saved["cursors"]), {str(self.log)})

    def test_truncation_or_inode_change_rebaselines_safely(self):
        self.log.touch()
        tracker = self.tracker()
        tracker.refresh((self.log,), now=10.0)
        self.append("execute_task:start session_id=session-a task_id=task-a")
        tracker.refresh((self.log,), now=11.0)
        replacement = self.root / "replacement.log"
        replacement.write_text(
            "execute_task:start session_id=session-b task_id=task-b\n",
            encoding="utf-8",
        )
        replacement.replace(self.log)

        rebaselined = tracker.refresh((self.log,), now=12.0)

        self.assertEqual([item.task_id for item in rebaselined.tasks], ["task-a"])
        self.append("progress_notice session_id=session-b task_id=task-b")
        refreshed = tracker.refresh((self.log,), now=13.0)
        self.assertEqual(
            {item.task_id for item in refreshed.tasks},
            {"task-a", "task-b"},
        )

    def test_ledger_contains_normalized_metadata_not_raw_prompt_or_log(self):
        self.log.touch()
        tracker = self.tracker()
        tracker.refresh((self.log,), now=10.0)
        self.append(
            "execute_task:start session_id=session-a task_id=task-a "
            "prompt=private-user-text tool_args=private-tool-data"
        )

        tracker.refresh((self.log,), now=11.0)

        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        serialized = json.dumps(saved, sort_keys=True)
        self.assertEqual(saved["tasks"][0]["session_id"], "session-a")
        self.assertEqual(saved["tasks"][0]["task_id"], "task-a")
        self.assertEqual(saved["tasks"][0]["state"], "running")
        self.assertNotIn("private-user-text", serialized)
        self.assertNotIn("private-tool-data", serialized)
        self.assertNotIn("raw", serialized)
        self.assertLessEqual(len(saved["tasks"]), 32)

    def test_tracker_reads_appends_from_two_project_logs(self):
        log_a = self.root / "project-a.log"
        log_b = self.root / "project-b.log"
        log_a.write_text("baseline\n", encoding="utf-8")
        log_b.write_text("baseline\n", encoding="utf-8")
        tracker = self.tracker()
        tracker.refresh((log_a, log_b), now=1.0)
        with log_a.open("a", encoding="utf-8") as stream:
            stream.write(
                "execute_task:start session_id=session-a task_id=task-a\n"
            )
        with log_b.open("a", encoding="utf-8") as stream:
            stream.write(
                "execute_task:start session_id=session-b task_id=task-b\n"
            )

        snapshot = tracker.refresh((log_a, log_b), now=2.0)

        self.assertEqual(
            {(item.session_id, item.task_id) for item in snapshot.tasks},
            {("session-a", "task-a"), ("session-b", "task-b")},
        )

    def test_completed_history_never_evicts_active_tasks_from_bounded_ledger(self):
        self.log.touch()
        tracker = self.tracker()
        tracker.refresh((self.log,), now=1.0)
        self.append(
            *(
                "execute_task:start session_id=session-active "
                "task_id=active-{}".format(index)
                for index in range(3)
            )
        )
        tracker.refresh((self.log,), now=2.0)

        for index in range(40):
            self.append(
                "execute_task:start session_id=session-history "
                "task_id=history-{}".format(index),
                "[append_resume_listener] exiting session_id=session-history "
                "task_id=history-{}".format(index),
            )
            tracker.refresh((self.log,), now=float(index + 3))

        snapshot = tracker.refresh((self.log,), now=100.0)
        active = ordered_active_tasks(snapshot.tasks)
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))

        self.assertEqual(
            {item.task_id for item in active},
            {"active-0", "active-1", "active-2"},
        )
        self.assertEqual(len(active), 3)
        self.assertEqual(
            {item.task_id for item in select_transport_tasks(snapshot.tasks)},
            {"active-0", "active-1", "active-2"},
        )
        self.assertEqual(len(saved["tasks"]), 32)
        self.assertEqual(
            {
                item["task_id"]
                for item in saved["tasks"]
                if item["state"] != "completed"
            },
            {"active-0", "active-1", "active-2"},
        )

    def test_two_project_models_are_correlated_and_fallback_uses_newest_event(self):
        log_a = self.root / "a-project.log"
        log_z = self.root / "z-project.log"
        log_a.write_text("baseline\n", encoding="utf-8")
        log_z.write_text("baseline\n", encoding="utf-8")
        tracker = self.tracker()
        tracker.refresh((log_a, log_z), now=1.0)
        with log_a.open("a", encoding="utf-8") as stream:
            stream.write(
                "2026-07-24 10:00:04 model_info: CustomModel { "
                'display_model_name: Some("New Model"), '
                'reasoning_effort: Some("high") }\n'
                "2026-07-24 10:00:05 execute_task:start "
                "session_id=session-a task_id=task-a\n"
            )
        with log_z.open("a", encoding="utf-8") as stream:
            stream.write(
                "2026-07-24 10:00:01 model_info: CustomModel { "
                'display_model_name: Some("Old Model"), '
                'reasoning_effort: Some("low") }\n'
                "2026-07-24 10:00:02 execute_task:start "
                "session_id=session-z task_id=task-z\n"
            )

        snapshot = tracker.refresh((log_a, log_z), now=2.0)
        by_task = {item.task_id: item for item in snapshot.tasks}

        self.assertEqual(snapshot.model, "New Model")
        self.assertEqual(snapshot.effort, "high")
        self.assertEqual(by_task["task-a"].model, "New Model")
        self.assertEqual(by_task["task-a"].effort, "high")
        self.assertEqual(by_task["task-z"].model, "Old Model")
        self.assertEqual(by_task["task-z"].effort, "low")

    def test_timestamped_activity_orders_tasks_when_identifier_order_opposes_it(self):
        log_a = self.root / "a-project.log"
        log_z = self.root / "z-project.log"
        log_a.write_text("baseline\n", encoding="utf-8")
        log_z.write_text("baseline\n", encoding="utf-8")
        tracker = self.tracker()
        tracker.refresh((log_a, log_z), now=1.0)
        with log_a.open("a", encoding="utf-8") as stream:
            stream.write(
                "2026-07-24 10:00:02 execute_task:start "
                "session_id=session-z task_id=z-new\n"
            )
        with log_z.open("a", encoding="utf-8") as stream:
            stream.write(
                "2026-07-24 10:00:01 execute_task:start "
                "session_id=session-a task_id=a-old\n"
            )

        snapshot = tracker.refresh((log_a, log_z), now=2.0)
        ordered = ordered_active_tasks(snapshot.tasks)

        self.assertEqual([item.task_id for item in ordered], ["z-new", "a-old"])
        self.assertGreater(ordered[0].updated_at, ordered[1].updated_at)

    def test_model_after_task_is_not_backdated_onto_that_task(self):
        self.log.write_text("baseline\n", encoding="utf-8")
        tracker = self.tracker()
        tracker.refresh((self.log,), now=1.0)
        self.append(
            "2026-07-24 10:00:01 execute_task:start "
            "session_id=session-a task_id=task-a",
            "2026-07-24 10:00:02 model_info: CustomModel { "
            'display_model_name: Some("Future Model"), '
            'reasoning_effort: Some("high") }',
        )

        snapshot = tracker.refresh((self.log,), now=2.0)

        self.assertEqual(snapshot.model, "Future Model")
        self.assertEqual(snapshot.tasks[0].model, "")
        self.assertEqual(snapshot.tasks[0].effort, "")


if __name__ == "__main__":
    unittest.main()
