import datetime
import json
import pathlib
import sqlite3
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from task_alerts import TaskAlertTracker


UTC = datetime.timezone.utc


class Clock:
    def __init__(self, value):
        self.value = value

    def __call__(self):
        return self.value


def create_state_db(path):
    connection = sqlite3.connect(path)
    connection.execute(
        """
        CREATE TABLE threads (
            id TEXT PRIMARY KEY,
            cwd TEXT NOT NULL,
            title TEXT NOT NULL,
            rollout_path TEXT NOT NULL,
            model_provider TEXT NOT NULL,
            thread_source TEXT,
            updated_at_ms INTEGER NOT NULL
        )
        """
    )
    connection.commit()
    connection.close()


def add_thread(state_db, thread_id, cwd, title, rollout, updated_at_ms):
    connection = sqlite3.connect(state_db)
    connection.execute(
        "INSERT INTO threads VALUES (?, ?, ?, ?, 'openai', NULL, ?)",
        (thread_id, cwd, title, str(rollout), updated_at_ms),
    )
    connection.commit()
    connection.close()


def append_event(path, event_type, timestamp, complete_line=True):
    item = {
        "timestamp": timestamp,
        "type": "event_msg",
        "payload": {"type": event_type},
    }
    with path.open("ab") as stream:
        stream.write(json.dumps(item).encode("utf-8"))
        if complete_line:
            stream.write(b"\n")


class TaskAlertTrackerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name)
        self.state_db = self.root / "state.sqlite"
        self.ledger = self.root / "alerts.json"
        self.rollout = self.root / "rollout.jsonl"
        self.rollout.touch()
        create_state_db(self.state_db)
        add_thread(
            self.state_db,
            "thread-a",
            "/workspace/StopWatch",
            "Codex status monitor",
            self.rollout,
            1,
        )
        self.clock = Clock(datetime.datetime(2026, 7, 12, 0, 0, tzinfo=UTC))

    def tearDown(self):
        self.temporary.cleanup()

    def tracker(self):
        return TaskAlertTracker(
            state_db=self.state_db,
            ledger_path=self.ledger,
            now=self.clock,
            local_timezone=UTC,
        )

    def test_first_refresh_baselines_existing_history_without_alerting(self):
        append_event(self.rollout, "task_complete", "2026-07-11T23:59:00Z")

        snapshot = self.tracker().refresh()

        self.assertEqual(snapshot.pending_count, 0)
        self.assertEqual(snapshot.handled_count, 0)
        self.assertEqual(snapshot.alerts, ())
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(saved["cursors"]["thread-a"]["offset"], self.rollout.stat().st_size)

    def test_completion_creates_pending_and_reply_handles_same_conversation(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(self.rollout, "task_complete", "2026-07-12T00:01:00Z")

        pending = tracker.refresh()

        self.assertEqual(pending.pending_count, 1)
        self.assertEqual(pending.alerts[0].project, "StopWatch")
        self.assertEqual(pending.alerts[0].title, "Codex status monitor")
        self.assertEqual(
            tracker.pending_watch_notification_ids(),
            ("thread-a:2026-07-12T00:01:00Z",),
        )

        append_event(self.rollout, "user_message", "2026-07-12T00:02:00Z")
        handled = tracker.refresh()

        self.assertEqual(handled.pending_count, 0)
        self.assertEqual(handled.handled_count, 1)
        self.assertEqual(handled.alerts[0].state, "handled")
        self.assertEqual(handled.alerts[0].handled_at, "2026-07-12T00:02:00Z")
        self.assertEqual(tracker.pending_watch_notification_ids(), ())

    def test_reply_does_not_clear_another_conversation_in_same_project(self):
        rollout_b = self.root / "rollout-b.jsonl"
        rollout_b.touch()
        add_thread(
            self.state_db,
            "thread-b",
            "/workspace/StopWatch",
            "Bluetooth setup",
            rollout_b,
            2,
        )
        tracker = self.tracker()
        tracker.refresh()
        append_event(self.rollout, "task_complete", "2026-07-12T00:03:00Z")
        append_event(rollout_b, "task_complete", "2026-07-12T00:04:00Z")
        tracker.refresh()

        append_event(self.rollout, "user_message", "2026-07-12T00:05:00Z")
        snapshot = tracker.refresh()

        by_thread = {alert.thread_id: alert for alert in snapshot.alerts}
        self.assertEqual(by_thread["thread-a"].state, "handled")
        self.assertEqual(by_thread["thread-b"].state, "pending")

    def test_restart_resumes_saved_cursor_and_deduplicates(self):
        first = self.tracker()
        first.refresh()
        append_event(self.rollout, "task_complete", "2026-07-12T00:06:00Z")
        self.assertEqual(first.refresh().pending_count, 1)

        restarted = self.tracker()
        self.assertEqual(restarted.refresh().pending_count, 1)
        append_event(self.rollout, "task_complete", "2026-07-12T00:07:00Z")
        snapshot = restarted.refresh()

        self.assertEqual(snapshot.pending_count, 2)
        self.assertEqual(len({alert.id for alert in snapshot.alerts}), 2)

    def test_new_thread_created_during_downtime_is_recovered(self):
        tracker = self.tracker()
        tracker.refresh()
        self.clock.value = datetime.datetime(2026, 7, 12, 0, 10, tzinfo=UTC)
        rollout_b = self.root / "new-thread.jsonl"
        append_event(rollout_b, "task_complete", "2026-07-12T00:09:00Z")
        add_thread(
            self.state_db,
            "thread-new",
            "/workspace/webshot",
            "UI review",
            rollout_b,
            3,
        )

        snapshot = self.tracker().refresh()

        self.assertEqual(snapshot.pending_count, 1)
        self.assertEqual(snapshot.alerts[0].project, "webshot")

    def test_mark_watch_notified_is_persistent(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(self.rollout, "task_complete", "2026-07-12T00:11:00Z")
        tracker.refresh()
        alert_ids = tracker.pending_watch_notification_ids()

        tracker.mark_watch_notified(alert_ids)

        self.assertEqual(tracker.pending_watch_notification_ids(), ())
        self.assertEqual(self.tracker().pending_watch_notification_ids(), ())

    def test_incomplete_trailing_line_is_retried(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:12:00Z",
            complete_line=False,
        )
        self.assertEqual(tracker.refresh().pending_count, 0)

        with self.rollout.open("ab") as stream:
            stream.write(b"\n")

        self.assertEqual(tracker.refresh().pending_count, 1)

    def test_corrupt_ledger_is_moved_aside_and_baselined(self):
        self.ledger.write_text("not json", encoding="utf-8")
        append_event(self.rollout, "task_complete", "2026-07-12T00:13:00Z")

        snapshot = self.tracker().refresh()

        self.assertEqual(snapshot.alerts, ())
        self.assertTrue(list(self.root.glob("alerts.json.corrupt-*")))

    def test_alerts_older_than_seven_local_days_are_pruned(self):
        self.clock.value = datetime.datetime(2026, 7, 3, 0, 0, tzinfo=UTC)
        tracker = self.tracker()
        tracker.refresh()
        append_event(self.rollout, "task_complete", "2026-07-03T00:01:00Z")
        self.assertEqual(tracker.refresh().pending_count, 1)

        self.clock.value = datetime.datetime(2026, 7, 12, 0, 0, tzinfo=UTC)
        self.assertEqual(tracker.refresh().alerts, ())
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(saved["alerts"], [])


if __name__ == "__main__":
    unittest.main()
