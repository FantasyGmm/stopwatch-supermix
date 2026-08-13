import datetime
import json
import pathlib
import sqlite3
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from codex_adapter import CodexPaths, CodexStateCollector
from task_alerts import TaskAlert, TaskAlertSnapshot
from task_routes import route_id_for_thread


class FakeAlertTracker:
    def __init__(self, snapshot):
        self.snapshot = snapshot
        self.notified = []
        self.waits = []

    def refresh(self):
        return self.snapshot

    def pending_watch_notification_ids(self):
        return tuple(
            alert.id
            for alert in self.snapshot.alerts
            if alert.state == "pending" and not alert.watch_notified
        )

    def mark_watch_notified(self, alert_ids):
        self.notified.extend(alert_ids)

    def wait_for_change(self, timeout):
        self.waits.append(timeout)
        return True


def create_thread_state_db(path, rows):
    connection = sqlite3.connect(path)
    connection.execute(
        "CREATE TABLE threads ("
        "id TEXT PRIMARY KEY, model TEXT, reasoning_effort TEXT)"
    )
    connection.executemany("INSERT INTO threads VALUES (?, ?, ?)", rows)
    connection.commit()
    connection.close()


def running_alert(thread_id, timestamp):
    return TaskAlert(
        id="{}:{}".format(thread_id, timestamp),
        thread_id=thread_id,
        project="project-{}".format(thread_id),
        title="Task {}".format(thread_id),
        progress_at=timestamp,
        handled_at=None,
        state="running",
        watch_notified=True,
        outcome="running",
    )


class CodexStateCollectorTests(unittest.TestCase):
    def test_wait_for_change_delegates_to_alert_tracker(self):
        tracker = FakeAlertTracker(TaskAlertSnapshot())
        collector = CodexStateCollector(alert_tracker=tracker)

        changed = collector.wait_for_change(0.25)

        self.assertTrue(changed)
        self.assertEqual(tracker.waits, [0.25])

    def collector_for_running_models(self, root, alerts, rows):
        state_db = root / "state.sqlite"
        create_thread_state_db(state_db, rows)
        config = root / "config.toml"
        config.write_text(
            'model = "gpt-fallback"\nmodel_reasoning_effort = "low"\n',
            encoding="utf-8",
        )
        snapshot = TaskAlertSnapshot(
            running_count=len(alerts),
            alerts=tuple(alerts),
        )
        return CodexStateCollector(
            paths=CodexPaths(
                config=config,
                auth=root / "missing-auth.json",
                logs=root / "missing-logs.sqlite",
                sessions=root / "missing-sessions",
                state_db=state_db,
                model_selection_cache=root / "model-selection.json",
            ),
            alert_tracker=FakeAlertTracker(snapshot),
            quota_fetcher=lambda: {},
        )

    def test_selects_majority_model_effort_pair_from_running_conversations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            alerts = [
                running_alert("a", "2026-07-12T00:01:00Z"),
                running_alert("b", "2026-07-12T00:02:00Z"),
                running_alert("c", "2026-07-12T00:03:00Z"),
                running_alert("d", "2026-07-12T00:04:00Z"),
            ]
            collector = self.collector_for_running_models(
                root,
                alerts,
                (
                    ("a", "gpt-5.6-sol", "xhigh"),
                    ("b", "gpt-5.6-sol", "xhigh"),
                    ("c", "gpt-5.6-sol", "xhigh"),
                    ("d", "gpt-5.6-sol", "medium"),
                ),
            )

            snapshot = collector.get_state()

            self.assertEqual(snapshot["codex_model"], "gpt-5.6-sol")
            self.assertEqual(snapshot["codex_effort"], "xhigh")

    def test_tied_pairs_use_most_recent_running_conversation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            collector = self.collector_for_running_models(
                root,
                (
                    running_alert("older", "2026-07-12T00:01:00Z"),
                    running_alert("newer", "2026-07-12T00:02:00Z"),
                ),
                (
                    ("older", "gpt-5.6-sol", "xhigh"),
                    ("newer", "gpt-5.6-sol", "medium"),
                ),
            )

            snapshot = collector.get_state()

            self.assertEqual(snapshot["codex_model"], "gpt-5.6-sol")
            self.assertEqual(snapshot["codex_effort"], "medium")

    def test_no_running_conversations_restore_persisted_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            first = self.collector_for_running_models(
                root,
                (running_alert("a", "2026-07-12T00:01:00Z"),),
                (("a", "gpt-5.6-sol", "xhigh"),),
            )
            first.get_state()
            cache = root / "model-selection.json"
            self.assertEqual(
                json.loads(cache.read_text(encoding="utf-8")),
                {"model": "gpt-5.6-sol", "effort": "xhigh"},
            )

            second = CodexStateCollector(
                paths=CodexPaths(
                    config=root / "config.toml",
                    auth=root / "missing-auth.json",
                    logs=root / "missing-logs.sqlite",
                    sessions=root / "missing-sessions",
                    state_db=root / "missing-state.sqlite",
                    model_selection_cache=cache,
                ),
                alert_tracker=FakeAlertTracker(TaskAlertSnapshot()),
                quota_fetcher=lambda: {},
            )

            snapshot = second.get_state()

            self.assertEqual(snapshot["codex_model"], "gpt-5.6-sol")
            self.assertEqual(snapshot["codex_effort"], "xhigh")

    def test_uses_newest_codex_session_event_not_largest_reset_time(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            sessions = root / "sessions" / "2026" / "07" / "12"
            sessions.mkdir(parents=True)
            session = sessions / "rollout.jsonl"
            session.write_text(
                "\n".join(
                    [
                        json.dumps(
                            {
                                "timestamp": "2026-07-11T16:29:10Z",
                                "type": "event_msg",
                                "payload": {
                                    "type": "token_count",
                                    "rate_limits": {
                                        "primary": {
                                            "used_percent": 0,
                                            "resets_at": 200,
                                        },
                                        "secondary": {
                                            "used_percent": 1,
                                            "resets_at": 300,
                                        },
                                        "plan_type": "prolite",
                                    },
                                },
                            }
                        ),
                        json.dumps(
                            {
                                "timestamp": "2026-07-11T16:30:15Z",
                                "type": "event_msg",
                                "payload": {
                                    "type": "token_count",
                                    "rate_limits": {
                                        "primary": {
                                            "used_percent": 98,
                                            "resets_at": 100,
                                        },
                                        "secondary": {
                                            "used_percent": 7,
                                            "resets_at": 250,
                                        },
                                        "plan_type": "prolite",
                                    },
                                },
                            }
                        ),
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            collector = CodexStateCollector(
                paths=CodexPaths(
                    config=root / "missing-config.toml",
                    auth=root / "missing-auth.json",
                    logs=root / "missing-logs.sqlite",
                    sessions=sessions,
                    state_db=root / "missing-state.sqlite",
                    model_selection_cache=root / "model-selection.json",
                ),
                quota_fetcher=lambda: {
                    "five_hour_used_pct": 32,
                    "week_used_pct": 7,
                },
            )

            snapshot = collector.get_state()

        self.assertEqual(snapshot["codex_5h_left_pct"], 2)
        self.assertEqual(snapshot["codex_week_left_pct"], 93)

    def test_named_model_quota_does_not_override_account_quota(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            sessions = root / "sessions"
            sessions.mkdir()
            (sessions / "rollout.jsonl").write_text(
                "\n".join(
                    [
                        json.dumps(
                            {
                                "timestamp": "2026-07-28T16:29:29Z",
                                "type": "event_msg",
                                "payload": {
                                    "type": "token_count",
                                    "rate_limits": {
                                        "primary": {
                                            "used_percent": 9,
                                            "window_minutes": 10080,
                                            "resets_at": 1785848775,
                                        },
                                        "secondary": None,
                                        "plan_type": "pro",
                                    },
                                },
                            }
                        ),
                        json.dumps(
                            {
                                "timestamp": "2026-07-28T16:29:38Z",
                                "type": "event_msg",
                                "payload": {
                                    "type": "token_count",
                                    "rate_limits": {
                                        "primary": {
                                            "used_percent": 0,
                                            "window_minutes": 10080,
                                            "resets_at": 1785860973,
                                        },
                                        "secondary": None,
                                        "limit_name": "GPT-5.3-Codex-Spark",
                                    },
                                },
                            }
                        ),
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            collector = CodexStateCollector(
                paths=CodexPaths(
                    config=root / "missing-config.toml",
                    auth=root / "missing-auth.json",
                    logs=root / "missing-logs.sqlite",
                    sessions=sessions,
                    state_db=root / "missing-state.sqlite",
                    model_selection_cache=root / "model-selection.json",
                ),
                quota_fetcher=lambda: {"week_used_pct": 37},
            )

            snapshot = collector.get_state()

        self.assertEqual(snapshot["codex_week_left_pct"], 91)

    def test_classifies_week_only_primary_window_by_duration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            sessions = root / "sessions"
            sessions.mkdir()
            (sessions / "rollout.jsonl").write_text(
                json.dumps(
                    {
                        "timestamp": "2026-07-13T00:00:00Z",
                        "type": "event_msg",
                        "payload": {
                            "type": "token_count",
                            "rate_limits": {
                                "primary": {
                                    "used_percent": 68,
                                    "window_minutes": 10080,
                                    "resets_at": 1784392063,
                                },
                                "secondary": None,
                                "plan_type": "prolite",
                            },
                        },
                    }
                )
                + "\n",
                encoding="utf-8",
            )
            collector = CodexStateCollector(
                paths=CodexPaths(
                    config=root / "missing-config.toml",
                    auth=root / "missing-auth.json",
                    logs=root / "missing-logs.sqlite",
                    sessions=sessions,
                    state_db=root / "missing-state.sqlite",
                    model_selection_cache=root / "model-selection.json",
                ),
                quota_fetcher=lambda: {},
            )

            snapshot = collector.get_state()

        self.assertIsNone(snapshot["codex_5h_left_pct"])
        self.assertEqual(snapshot["codex_week_left_pct"], 32)

    def test_drops_http_quota_after_failed_refresh_exceeds_stale_limit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            clock = [1000.0]
            responses = iter(
                [
                    {"five_hour_used_pct": 28, "week_used_pct": 36},
                    {},
                ]
            )
            collector = CodexStateCollector(
                paths=CodexPaths(
                    config=root / "missing-config.toml",
                    auth=root / "missing-auth.json",
                    logs=root / "missing-logs.sqlite",
                    sessions=root / "missing-sessions",
                    state_db=root / "missing-state.sqlite",
                    model_selection_cache=root / "model-selection.json",
                ),
                now=lambda: clock[0],
                quota_fetcher=lambda: next(responses),
            )

            first = collector.get_state()
            clock[0] += 121
            stale = collector.get_state()

        self.assertEqual(first["codex_5h_left_pct"], 72)
        self.assertIsNone(stale["codex_5h_left_pct"])
        self.assertIsNone(stale["codex_week_left_pct"])

    def test_builds_watch_snapshot_from_local_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            config = root / "config.toml"
            config.write_text(
                'model = "gpt-fallback"\nmodel_reasoning_effort = "low"\n',
                encoding="utf-8",
            )
            logs = root / "logs.sqlite"
            connection = sqlite3.connect(logs)
            connection.execute(
                "CREATE TABLE logs (id INTEGER PRIMARY KEY, feedback_log_body TEXT)"
            )
            connection.execute(
                "INSERT INTO logs(feedback_log_body) VALUES (?)",
                ("model=gpt-5.3 codex.turn.reasoning_effort=high",),
            )
            connection.commit()
            connection.close()

            alerts = TaskAlertSnapshot(
                pending_count=1,
                running_count=1,
                completed_count=1,
                revision=4,
                alerts=(
                    TaskAlert(
                        id="thread-a:1",
                        thread_id="thread-a",
                        project="StopWatch",
                        title="Codex status monitor",
                        progress_at="2026-07-12T00:15:00Z",
                        handled_at=None,
                        state="pending",
                        watch_notified=False,
                        outcome="waiting",
                        turn_id="turn-a",
                        last_reply="Exact stable reply A",
                    ),
                    TaskAlert(
                        id="thread-b:1",
                        thread_id="thread-b",
                        project="docs",
                        title="Update guide",
                        progress_at="2026-07-12T00:12:00Z",
                        handled_at=None,
                        state="running",
                        watch_notified=True,
                        outcome="running",
                        turn_id="turn-b",
                        last_reply="Exact stable reply B",
                    ),
                    TaskAlert(
                        id="thread-c:1",
                        thread_id="thread-c",
                        project="webshot",
                        title="UI review",
                        progress_at="2026-07-12T00:08:00Z",
                        handled_at="2026-07-12T00:09:00Z",
                        state="completed",
                        watch_notified=True,
                        outcome="completed",
                        turn_id="turn-c",
                        last_reply="Exact stable reply C",
                    ),
                ),
            )

            collector = CodexStateCollector(
                paths=CodexPaths(
                    config=config,
                    auth=root / "missing-auth.json",
                    logs=logs,
                    sessions=root / "missing-sessions",
                    state_db=root / "missing-state.sqlite",
                    model_selection_cache=root / "model-selection.json",
                ),
                alert_tracker=FakeAlertTracker(alerts),
                local_timezone=datetime.timezone(datetime.timedelta(hours=8)),
                quota_fetcher=lambda: {
                    "five_hour_used_pct": 28,
                    "week_used_pct": 36,
                },
            )

            snapshot = collector.get_state()

        self.assertEqual(snapshot["codex_model"], "gpt-5.3")
        self.assertEqual(snapshot["codex_effort"], "high")
        self.assertEqual(snapshot["codex_5h_left_pct"], 72)
        self.assertEqual(snapshot["codex_week_left_pct"], 64)
        self.assertNotIn("codex_today_min", snapshot)
        self.assertNotIn("codex_week_min", snapshot)
        self.assertEqual(snapshot["codex_alert_pending"], 1)
        self.assertEqual(snapshot["codex_alert_running"], 1)
        self.assertEqual(snapshot["codex_alert_completed"], 1)
        self.assertNotIn("codex_alert_handled", snapshot)
        self.assertEqual(snapshot["codex_alert_revision"], 4)
        self.assertEqual(
            snapshot["_codex_watch_notification_ids"],
            ("thread-a:1",),
        )
        self.assertEqual(
            snapshot["codex_alerts"],
            [
                {
                    "route": route_id_for_thread("thread-a"),
                    "thread_id": "thread-a",
                    "project": "StopWatch",
                    "title": "Codex status monitor",
                    "time": "08:15",
                    "state": "pending",
                    "outcome": "waiting",
                    "turn_id": "turn-a",
                    "last_reply": "Exact stable reply A",
                },
                {
                    "route": route_id_for_thread("thread-b"),
                    "thread_id": "thread-b",
                    "project": "docs",
                    "title": "Update guide",
                    "time": "08:12",
                    "state": "running",
                    "outcome": "running",
                    "turn_id": "turn-b",
                    "last_reply": "Exact stable reply B",
                },
                {
                    "route": route_id_for_thread("thread-c"),
                    "thread_id": "thread-c",
                    "project": "webshot",
                    "title": "UI review",
                    "time": "08:08",
                    "state": "completed",
                    "outcome": "completed",
                    "turn_id": "turn-c",
                    "last_reply": "Exact stable reply C",
                },
            ],
        )

    def test_corrupt_cache_and_missing_state_db_use_existing_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            config = root / "config.toml"
            config.write_text(
                'model = "gpt-fallback"\nmodel_reasoning_effort = "low"\n',
                encoding="utf-8",
            )
            cache = root / "model-selection.json"
            cache.write_text("not json", encoding="utf-8")
            collector = CodexStateCollector(
                paths=CodexPaths(
                    config=config,
                    auth=root / "missing-auth.json",
                    logs=root / "missing-logs.sqlite",
                    sessions=root / "missing-sessions",
                    state_db=root / "missing-state.sqlite",
                    model_selection_cache=cache,
                ),
                alert_tracker=FakeAlertTracker(
                    TaskAlertSnapshot(
                        running_count=1,
                        alerts=(
                            running_alert("missing", "2026-07-12T00:01:00Z"),
                        ),
                    )
                ),
                quota_fetcher=lambda: {},
            )

            snapshot = collector.get_state()

            self.assertEqual(snapshot["codex_model"], "gpt-fallback")
            self.assertEqual(snapshot["codex_effort"], "low")


if __name__ == "__main__":
    unittest.main()
