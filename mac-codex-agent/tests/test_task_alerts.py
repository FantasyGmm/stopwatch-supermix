import datetime
import json
import pathlib
import sqlite3
import sys
import tempfile
import unittest
from unittest.mock import patch


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from completion_classifier import CLASSIFIER_VERSION
from task_alerts import TaskAlert, TaskAlertTracker, classify_outcome


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


def update_thread_metadata(state_db, thread_id, cwd, title, updated_at_ms):
    connection = sqlite3.connect(state_db)
    connection.execute(
        "UPDATE threads SET cwd = ?, title = ?, updated_at_ms = ? WHERE id = ?",
        (cwd, title, updated_at_ms, thread_id),
    )
    connection.commit()
    connection.close()


def append_event(
    path,
    event_type,
    timestamp,
    complete_line=True,
    last_agent_message=None,
    turn_id=None,
):
    payload = {"type": event_type}
    if last_agent_message is not None:
        payload["last_agent_message"] = last_agent_message
    if turn_id is not None:
        payload["turn_id"] = turn_id
    item = {
        "timestamp": timestamp,
        "type": "event_msg",
        "payload": payload,
    }
    with path.open("ab") as stream:
        stream.write(json.dumps(item).encode("utf-8"))
        if complete_line:
            stream.write(b"\n")


def append_function_call(
    path,
    timestamp,
    turn_id,
    name,
    arguments,
    call_id,
    include_turn_metadata=True,
):
    payload = {
        "type": "function_call",
        "name": name,
        "arguments": json.dumps(arguments),
        "call_id": call_id,
    }
    if include_turn_metadata:
        payload["internal_chat_message_metadata_passthrough"] = {
            "turn_id": turn_id
        }
    item = {
        "timestamp": timestamp,
        "type": "response_item",
        "payload": payload,
    }
    with path.open("ab") as stream:
        stream.write(json.dumps(item).encode("utf-8") + b"\n")


def append_function_output(
    path,
    timestamp,
    turn_id,
    call_id,
    include_turn_metadata=True,
):
    payload = {
        "type": "function_call_output",
        "call_id": call_id,
        "output": "answered",
    }
    if include_turn_metadata:
        payload["internal_chat_message_metadata_passthrough"] = {
            "turn_id": turn_id
        }
    item = {
        "timestamp": timestamp,
        "type": "response_item",
        "payload": payload,
    }
    with path.open("ab") as stream:
        stream.write(json.dumps(item).encode("utf-8") + b"\n")


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

    def tracker(self, **overrides):
        options = {
            "state_db": self.state_db,
            "ledger_path": self.ledger,
            "now": self.clock,
            "local_timezone": UTC,
            "thread_name_loader": lambda: {},
        }
        options.update(overrides)
        return TaskAlertTracker(
            **options,
        )

    def downgrade_classifier_ledger(self, **alert_updates):
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        saved["classifier_version"] = CLASSIFIER_VERSION - 1
        if alert_updates:
            saved["alerts"][-1].update(alert_updates)
        self.ledger.write_text(json.dumps(saved), encoding="utf-8")

    def test_classifier_upgrade_reclassifies_latest_completion_once(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-29T13:59:00Z",
            turn_id="turn-a",
        )
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-29T13:59:06Z",
            turn_id="turn-a",
            last_agent_message=(
                "首次添加 Agent 时，你希望如何确认？\n\n"
                "1. 验证码。\n2. 配对窗口。\n3. 系统配对。\n\n"
                "我推荐第 1 种。"
            ),
        )
        tracker.refresh()
        self.downgrade_classifier_ledger(
            state="completed",
            outcome="completed",
            reason="completion_only",
            handled_at="2026-07-29T13:59:06Z",
            watch_notified=True,
        )

        migrated = self.tracker().refresh()

        self.assertEqual(migrated.pending_count, 1)
        self.assertEqual(migrated.alerts[0].state, "pending")
        self.assertEqual(
            self.tracker().pending_watch_notification_ids(),
            ("thread-a:2026-07-29T13:59:06Z",),
        )
        persisted = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(persisted["classifier_version"], CLASSIFIER_VERSION)
        self.assertEqual(persisted["classifier_retry_threads"], [])

    def test_classifier_upgrade_recovers_reply_missing_from_older_ledger(self):
        reply = "精确回复：第一行\n第二行 🕰️"
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:01:00Z",
            last_agent_message=reply,
        )
        tracker.refresh()
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        saved["classifier_version"] = CLASSIFIER_VERSION - 1
        saved["alerts"][-1].pop("last_reply", None)
        self.ledger.write_text(json.dumps(saved), encoding="utf-8")

        reconciled = self.tracker().refresh()

        self.assertEqual(reconciled.alerts[0].last_reply, reply)
        persisted = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(persisted["alerts"][-1]["last_reply"], reply)

    def test_reply_metadata_upgrade_recovers_reply_from_unchanged_v3_ledger(self):
        reply = "旧版账本回复\n保持原样 🕰️"
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:01:00Z",
            last_agent_message=reply,
        )
        tracker.refresh()
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(saved["version"], 3)
        self.assertEqual(saved["classifier_version"], CLASSIFIER_VERSION)
        saved["alerts"][-1].pop("last_reply", None)
        saved.pop("reply_metadata_version", None)
        saved.pop("reply_metadata_retry_threads", None)
        self.ledger.write_text(json.dumps(saved), encoding="utf-8")

        reconciled = self.tracker().refresh()

        self.assertEqual(reconciled.alerts[0].last_reply, reply)
        persisted = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(persisted["reply_metadata_version"], 1)
        self.assertEqual(persisted["reply_metadata_retry_threads"], [])
        self.assertEqual(persisted["alerts"][-1]["last_reply"], reply)

    def test_reply_metadata_upgrade_backfills_deployed_running_alert(self):
        reply = "previous stable reply\nwith exact formatting"
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:01:00Z",
            last_agent_message=reply,
        )
        append_event(self.rollout, "user_message", "2026-07-12T00:01:30Z")
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:02:00Z",
            turn_id="turn-running",
        )
        tracker.refresh()
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(saved["version"], 3)
        self.assertEqual(saved["classifier_version"], CLASSIFIER_VERSION)
        for alert in saved["alerts"]:
            alert.pop("last_reply", None)
        saved.pop("reply_metadata_version", None)
        saved.pop("reply_metadata_retry_threads", None)
        self.ledger.write_text(json.dumps(saved), encoding="utf-8")

        migrated = self.tracker().refresh()

        running = next(alert for alert in migrated.alerts if alert.state == "running")
        self.assertEqual(running.turn_id, "turn-running")
        self.assertEqual(running.last_reply, reply)

    def test_reply_metadata_upgrade_preserves_undiscovered_thread_for_retry(self):
        reply = "reply recovered after database returns"
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:01:00Z",
            last_agent_message=reply,
        )
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:02:00Z",
            turn_id="turn-running",
        )
        tracker.refresh()
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        for alert in saved["alerts"]:
            alert.pop("last_reply", None)
        saved.pop("reply_metadata_version", None)
        saved.pop("reply_metadata_retry_threads", None)
        self.ledger.write_text(json.dumps(saved), encoding="utf-8")

        with patch(
            "task_alerts.sqlite3.connect",
            side_effect=sqlite3.OperationalError("database temporarily unavailable"),
        ):
            unavailable = self.tracker().refresh()

        self.assertIsNone(unavailable.alerts[0].last_reply)
        waiting = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(waiting["reply_metadata_version"], 1)
        self.assertEqual(waiting["reply_metadata_retry_threads"], ["thread-a"])

        recovered = self.tracker().refresh()

        self.assertEqual(recovered.alerts[0].last_reply, reply)
        persisted = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(persisted["reply_metadata_retry_threads"], [])

    def test_reply_metadata_upgrade_retries_incomplete_rollout(self):
        reply = "retry-safe reply"
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:01:00Z",
            last_agent_message=reply,
        )
        tracker.refresh()
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        saved["alerts"][-1].pop("last_reply", None)
        saved.pop("reply_metadata_version", None)
        saved.pop("reply_metadata_retry_threads", None)
        self.ledger.write_text(json.dumps(saved), encoding="utf-8")
        with self.rollout.open("ab") as stream:
            stream.write(b'{"ignored":true}')

        retrying = self.tracker().refresh()

        self.assertIsNone(retrying.alerts[0].last_reply)
        persisted = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(persisted["reply_metadata_version"], 1)
        self.assertEqual(persisted["reply_metadata_retry_threads"], ["thread-a"])

        with self.rollout.open("ab") as stream:
            stream.write(b"\n")
        recovered = self.tracker().refresh()

        self.assertEqual(recovered.alerts[0].last_reply, reply)
        persisted = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(persisted["reply_metadata_retry_threads"], [])

    def test_classifier_upgrade_does_not_resurrect_after_later_user_message(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:01:00Z",
            turn_id="turn-a",
        )
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:01:01Z",
            turn_id="turn-a",
            last_agent_message="请选择一个方案。",
        )
        tracker.refresh()
        append_event(self.rollout, "user_message", "2026-07-12T00:01:02Z")
        tracker.refresh()
        self.downgrade_classifier_ledger()

        migrated = self.tracker().refresh()

        self.assertEqual(migrated.pending_count, 0)
        self.assertEqual(migrated.completed_count, 1)
        persisted = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(persisted["classifier_version"], CLASSIFIER_VERSION)

    def test_classifier_upgrade_preserves_already_notified_pending(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:02:00Z",
            last_agent_message="请确认是否继续？",
        )
        tracker.refresh()
        alert_ids = tracker.pending_watch_notification_ids()
        tracker.mark_watch_notified(alert_ids)
        self.downgrade_classifier_ledger()

        migrated = self.tracker().refresh()

        self.assertEqual(migrated.pending_count, 1)
        self.assertEqual(self.tracker().pending_watch_notification_ids(), ())
        self.assertTrue(migrated.alerts[0].watch_notified)

    def test_classifier_upgrade_is_idempotent_across_restart(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:03:00Z",
            last_agent_message="请选择 A 或 B。",
        )
        tracker.refresh()
        self.downgrade_classifier_ledger(
            state="completed",
            outcome="completed",
            reason="completion_only",
            handled_at="2026-07-12T00:03:00Z",
            watch_notified=True,
        )

        first = self.tracker().refresh()
        first_revision = first.revision
        second = self.tracker().refresh()

        self.assertEqual(first.pending_count, 1)
        self.assertEqual(second.pending_count, 1)
        self.assertEqual(second.revision, first_revision)
        self.assertEqual(
            self.tracker().pending_watch_notification_ids(),
            ("thread-a:2026-07-12T00:03:00Z",),
        )

    def test_classifier_upgrade_retries_after_incomplete_rollout(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:04:00Z",
            last_agent_message="请选择 A 或 B。",
        )
        tracker.refresh()
        self.downgrade_classifier_ledger(
            state="completed",
            outcome="completed",
            reason="completion_only",
            handled_at="2026-07-12T00:04:00Z",
            watch_notified=True,
        )
        with self.rollout.open("ab") as stream:
            stream.write(b'{"timestamp":"2026-07-12T00:05:00Z"')

        unchanged = self.tracker().refresh()

        self.assertEqual(unchanged.completed_count, 1)
        waiting_ledger = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(waiting_ledger["classifier_version"], CLASSIFIER_VERSION)
        self.assertEqual(waiting_ledger["classifier_retry_threads"], ["thread-a"])

        with self.rollout.open("ab") as stream:
            stream.write(b',"type":"event_msg","payload":{"type":"user_message"}}\n')
        retried = self.tracker().refresh()

        self.assertEqual(retried.pending_count, 0)
        retried_ledger = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(retried_ledger["classifier_version"], CLASSIFIER_VERSION)
        self.assertEqual(retried_ledger["classifier_retry_threads"], [])

    def test_classifies_only_the_final_paragraph(self):
        cases = (
            ("Build and tests passed.", "completed"),
            ("Build passed.\n\n可以确认这个方案吗？", "waiting"),
            ("Everything is ready.\n\n请回复确认后继续。", "waiting"),
            ("请重新插入手表的数据线。", "waiting"),
            ("任务尚未完成，等待回复。", "waiting"),
            ("当前停在配置步骤，需要进一步指令。", "waiting"),
            ("Please provide the missing token.", "waiting"),
            ("The log previously asked 'continue?'.\n\nThe repair is complete.", "completed"),
            ("方案确认后已继续实现，当前工作已经完成。", "completed"),
            ("", "completed"),
        )
        for message, expected in cases:
            with self.subTest(message=message):
                self.assertEqual(classify_outcome(message), expected)

    def test_waiting_classification_ignores_trailing_codex_directive(self):
        message = (
            "规格已经更新。\n\n"
            "请回复确认后继续。\n\n"
            '::git-commit{cwd="/workspace/stopwatch"}'
        )

        self.assertEqual(classify_outcome(message), "waiting")

    def test_review_then_continue_is_waiting(self):
        message = "请复核更新后的设计规格。确认后我继续实现。"

        self.assertEqual(classify_outcome(message), "waiting")

    def test_question_followed_by_recommendation_is_waiting(self):
        message = (
            "先确认一个布局选择：提醒区下移时，是否把分割线也一起下移？"
            "我建议一起移动，这样两个区域仍保持完整分组。"
        )

        self.assertEqual(classify_outcome(message), "waiting")

    def test_unfinished_plan_completion_is_pending_with_reason(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:00:10Z",
            turn_id="turn-a",
        )
        append_function_call(
            self.rollout,
            "2026-07-12T00:00:20Z",
            "turn-a",
            "update_plan",
            {
                "plan": [
                    {"step": "Show designs", "status": "completed"},
                    {"step": "Wait for selection", "status": "in_progress"},
                    {"step": "Implement selection", "status": "pending"},
                ]
            },
            "plan-a",
        )
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:00:30Z",
            last_agent_message="A, B, and C designs are ready.",
            turn_id="turn-a",
        )

        snapshot = tracker.refresh()

        self.assertEqual(snapshot.pending_count, 1)
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        alert = saved["alerts"][-1]
        self.assertEqual(alert["reason"], "structured_unfinished_plan")
        self.assertEqual(saved["turn_signals"], {})

    def test_latest_completed_plan_overrides_earlier_unfinished_plan(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:00:10Z",
            turn_id="turn-a",
        )
        append_function_call(
            self.rollout,
            "2026-07-12T00:00:20Z",
            "turn-a",
            "update_plan",
            {"plan": [{"step": "Implement", "status": "in_progress"}]},
            "plan-a",
        )
        append_function_call(
            self.rollout,
            "2026-07-12T00:00:25Z",
            "turn-a",
            "update_plan",
            {"plan": [{"step": "Implement", "status": "completed"}]},
            "plan-b",
        )
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:00:30Z",
            last_agent_message="Implementation and tests are complete.",
            turn_id="turn-a",
        )

        snapshot = tracker.refresh()

        self.assertEqual(snapshot.pending_count, 0)
        self.assertEqual(snapshot.completed_count, 1)

    def test_turn_signals_survive_tracker_restart(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:00:10Z",
            turn_id="turn-a",
        )
        append_function_call(
            self.rollout,
            "2026-07-12T00:00:20Z",
            "turn-a",
            "update_plan",
            {"plan": [{"step": "Wait for approval", "status": "pending"}]},
            "plan-a",
        )
        tracker.refresh()

        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertTrue(saved["turn_signals"]["thread-a"]["unfinished_plan"])

        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:00:30Z",
            last_agent_message="Specification is available.",
            turn_id="turn-a",
        )
        restarted = self.tracker().refresh()

        self.assertEqual(restarted.pending_count, 1)

    def test_unresolved_input_call_is_pending(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:00:10Z",
            turn_id="turn-a",
        )
        append_function_call(
            self.rollout,
            "2026-07-12T00:00:20Z",
            "turn-a",
            "request_user_input",
            {"questions": []},
            "input-a",
        )
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:00:30Z",
            last_agent_message="Waiting.",
            turn_id="turn-a",
        )

        tracker.refresh()
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))

        self.assertEqual(saved["alerts"][-1]["state"], "pending")
        self.assertEqual(saved["alerts"][-1]["reason"], "structured_input_request")

    def test_resolved_input_call_does_not_force_pending(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:00:10Z",
            turn_id="turn-a",
        )
        append_function_call(
            self.rollout,
            "2026-07-12T00:00:20Z",
            "turn-a",
            "request_user_input",
            {"questions": []},
            "input-a",
        )
        append_function_output(
            self.rollout,
            "2026-07-12T00:00:25Z",
            "turn-a",
            "input-a",
        )
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:00:30Z",
            last_agent_message="The selected option was implemented.",
            turn_id="turn-a",
        )

        snapshot = tracker.refresh()

        self.assertEqual(snapshot.pending_count, 0)
        self.assertEqual(snapshot.completed_count, 1)

    def test_input_call_without_turn_metadata_uses_current_turn(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:00:10Z",
            turn_id="turn-a",
        )
        append_function_call(
            self.rollout,
            "2026-07-12T00:00:20Z",
            "turn-a",
            "request_user_input",
            {"questions": []},
            "input-a",
            include_turn_metadata=False,
        )
        append_function_output(
            self.rollout,
            "2026-07-12T00:00:25Z",
            "turn-a",
            "input-a",
            include_turn_metadata=False,
        )
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:00:30Z",
            last_agent_message="The selected option was implemented.",
            turn_id="turn-a",
        )

        snapshot = tracker.refresh()

        self.assertEqual(snapshot.pending_count, 0)
        self.assertEqual(snapshot.completed_count, 1)

    def test_first_refresh_baselines_existing_history_without_alerting(self):
        append_event(self.rollout, "task_complete", "2026-07-11T23:59:00Z")

        snapshot = self.tracker().refresh()

        self.assertEqual(snapshot.pending_count, 0)
        self.assertEqual(snapshot.running_count, 0)
        self.assertEqual(snapshot.completed_count, 0)
        self.assertEqual(snapshot.alerts, ())
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(saved["cursors"]["thread-a"]["offset"], self.rollout.stat().st_size)

    def test_waiting_completion_creates_pending_and_reply_completes_conversation(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(self.rollout, "task_started", "2026-07-12T00:00:30Z")
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:01:00Z",
            last_agent_message="请确认是否继续？",
        )

        pending = tracker.refresh()

        self.assertEqual(pending.pending_count, 1)
        self.assertEqual(pending.alerts[0].project, "StopWatch")
        self.assertEqual(pending.alerts[0].title, "Codex status monitor")
        self.assertEqual(
            tracker.pending_watch_notification_ids(),
            ("thread-a:2026-07-12T00:01:00Z",),
        )

        append_event(self.rollout, "user_message", "2026-07-12T00:02:00Z")
        completed = tracker.refresh()

        self.assertEqual(completed.pending_count, 0)
        self.assertEqual(completed.completed_count, 1)
        self.assertEqual(completed.alerts[0].state, "completed")
        self.assertEqual(completed.alerts[0].handled_at, "2026-07-12T00:02:00Z")
        self.assertEqual(tracker.pending_watch_notification_ids(), ())

    def test_started_then_user_message_remains_running(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(self.rollout, "task_started", "2026-07-12T00:01:00Z")
        append_event(self.rollout, "user_message", "2026-07-12T00:01:01Z")

        snapshot = tracker.refresh()

        self.assertEqual(snapshot.pending_count, 0)
        self.assertEqual(snapshot.running_count, 1)
        self.assertEqual(snapshot.completed_count, 0)
        self.assertEqual(snapshot.alerts[0].state, "running")

    def test_aborted_turn_retires_running_without_notification(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:01:00Z",
            turn_id="turn-a",
        )
        append_event(
            self.rollout,
            "turn_aborted",
            "2026-07-12T00:01:05Z",
            turn_id="turn-a",
        )

        snapshot = tracker.refresh()

        self.assertEqual(snapshot.running_count, 0)
        self.assertEqual(snapshot.pending_count, 0)
        self.assertEqual(snapshot.completed_count, 1)
        self.assertEqual(tracker.pending_watch_notification_ids(), ())
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(saved["alerts"][-1]["reason"], "turn_aborted")
        self.assertEqual(saved["alerts"][-1]["handled_at"], "2026-07-12T00:01:05Z")

    def test_late_abort_cannot_clear_a_newer_turn(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:01:00Z",
            turn_id="turn-a",
        )
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:01:05Z",
            turn_id="turn-b",
        )
        append_event(
            self.rollout,
            "turn_aborted",
            "2026-07-12T00:01:06Z",
            turn_id="turn-a",
        )

        snapshot = tracker.refresh()

        self.assertEqual(snapshot.running_count, 1)
        self.assertEqual(snapshot.alerts[0].progress_at, "2026-07-12T00:01:05Z")

    def test_aborted_turn_is_idempotent_across_restart(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:01:00Z",
            turn_id="turn-a",
        )
        append_event(
            self.rollout,
            "turn_aborted",
            "2026-07-12T00:01:05Z",
            turn_id="turn-a",
        )
        first = tracker.refresh()

        restarted = self.tracker().refresh()

        self.assertEqual(restarted.running_count, 0)
        self.assertEqual(restarted.completed_count, 1)
        self.assertEqual(restarted.revision, first.revision)

    def test_abort_reducer_upgrade_repairs_stale_running_once(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:01:00Z",
            turn_id="turn-a",
        )
        tracker.refresh()
        append_event(
            self.rollout,
            "turn_aborted",
            "2026-07-12T00:01:05Z",
            turn_id="turn-a",
        )
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        saved["cursors"]["thread-a"]["offset"] = self.rollout.stat().st_size
        saved.pop("abort_reducer_version", None)
        saved.pop("abort_retry_threads", None)
        self.ledger.write_text(json.dumps(saved), encoding="utf-8")

        repaired = self.tracker().refresh()
        repaired_revision = repaired.revision
        restarted = self.tracker().refresh()

        self.assertEqual(repaired.running_count, 0)
        self.assertEqual(repaired.completed_count, 1)
        self.assertEqual(repaired.alerts[0].handled_at, "2026-07-12T00:01:05Z")
        persisted = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(persisted["alerts"][-1]["reason"], "turn_aborted")
        self.assertEqual(persisted["abort_reducer_version"], 1)
        self.assertEqual(persisted["abort_retry_threads"], [])
        self.assertEqual(restarted.revision, repaired_revision)

    def test_abort_reducer_upgrade_retries_an_incomplete_rollout(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:01:00Z",
            turn_id="turn-a",
        )
        tracker.refresh()
        append_event(
            self.rollout,
            "turn_aborted",
            "2026-07-12T00:01:05Z",
            turn_id="turn-a",
            complete_line=False,
        )
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        saved["cursors"]["thread-a"]["offset"] = self.rollout.stat().st_size
        saved.pop("abort_reducer_version", None)
        saved.pop("abort_retry_threads", None)
        self.ledger.write_text(json.dumps(saved), encoding="utf-8")

        retrying = self.tracker().refresh()

        self.assertEqual(retrying.running_count, 1)
        persisted = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(persisted["abort_reducer_version"], 1)
        self.assertEqual(persisted["abort_retry_threads"], ["thread-a"])

        with self.rollout.open("ab") as stream:
            stream.write(b"\n")
        repaired = self.tracker().refresh()

        self.assertEqual(repaired.running_count, 0)
        persisted = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(persisted["abort_retry_threads"], [])

    def test_normal_completion_becomes_completed_without_notification(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(self.rollout, "task_started", "2026-07-12T00:02:00Z")
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:02:30Z",
            last_agent_message="Build and tests passed.",
        )

        snapshot = tracker.refresh()

        self.assertEqual(snapshot.pending_count, 0)
        self.assertEqual(snapshot.running_count, 0)
        self.assertEqual(snapshot.completed_count, 1)
        self.assertEqual(snapshot.alerts[0].state, "completed")
        self.assertEqual(snapshot.alerts[0].outcome, "completed")
        self.assertEqual(tracker.pending_watch_notification_ids(), ())

    def test_task_alert_new_fields_default_to_none(self):
        alert = TaskAlert(
            id="alert-a",
            thread_id="thread-a",
            project="StopWatch",
            title="Monitor",
            progress_at="2026-07-12T00:02:30Z",
            handled_at=None,
            state="running",
            watch_notified=True,
            outcome="running",
        )

        self.assertIsNone(alert.turn_id)
        self.assertIsNone(alert.last_reply)

    def test_completion_retains_exact_reply_and_turn_in_snapshot(self):
        reply = "完成 ✅\n\n第二段保留空行。\n最后一行"
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:02:00Z",
            turn_id="turn-exact",
        )
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:02:30Z",
            turn_id="turn-exact",
            last_agent_message=reply,
        )

        alert = tracker.refresh().alerts[0]

        self.assertEqual(alert.turn_id, "turn-exact")
        self.assertEqual(alert.last_reply, reply)
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(saved["alerts"][-1]["last_reply"], reply)

    def test_new_running_alert_inherits_latest_same_thread_reply(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:01:00Z",
            last_agent_message="older reply",
        )
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:02:00Z",
            last_agent_message="最新回复\n原样保留",
        )
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:03:00Z",
            turn_id="turn-next",
        )

        alert = tracker.refresh().alerts[0]

        self.assertEqual(alert.state, "running")
        self.assertEqual(alert.turn_id, "turn-next")
        self.assertEqual(alert.last_reply, "最新回复\n原样保留")

    def test_new_running_alert_ignores_newer_other_thread_reply(self):
        rollout_b = self.root / "rollout-b.jsonl"
        rollout_b.touch()
        add_thread(
            self.state_db,
            "thread-b",
            "/workspace/OtherProject",
            "Other task",
            rollout_b,
            2,
        )
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:02:00Z",
            last_agent_message="thread-a stable reply",
        )
        append_event(
            rollout_b,
            "task_complete",
            "2026-07-12T00:02:30Z",
            last_agent_message="newer thread-b reply",
        )
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:03:00Z",
            turn_id="turn-next",
        )

        by_thread = {
            alert.thread_id: alert for alert in tracker.refresh().alerts
        }

        self.assertEqual(by_thread["thread-a"].last_reply, "thread-a stable reply")
        self.assertEqual(by_thread["thread-b"].last_reply, "newer thread-b reply")

    def test_nonstring_completion_reply_is_not_persisted_or_coerced(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:02:30Z",
            last_agent_message={"text": "not a stable reply"},
        )

        alert = tracker.refresh().alerts[0]

        self.assertIsNone(alert.last_reply)
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertNotIn("last_reply", saved["alerts"][-1])

    def test_nonstring_completion_reply_content_is_not_logged(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:02:30Z",
            last_agent_message={"text": "SECRET_REPLY_CONTENT"},
        )

        with self.assertLogs("task_alerts", level="INFO") as captured:
            tracker.refresh()

        self.assertEqual(
            captured.output,
            [
                "INFO:task_alerts:Task alert classified: "
                "thread=thread-a outcome=completed reason=completion_only"
            ],
        )
        self.assertNotIn("SECRET_REPLY_CONTENT", "\n".join(captured.output))

    def test_ledger_without_turn_or_reply_keys_remains_compatible(self):
        tracker = self.tracker()
        tracker.refresh()
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        saved["alerts"] = [
            {
                "id": "thread-a:2026-07-12T00:02:00Z",
                "thread_id": "thread-a",
                "project": "StopWatch",
                "title": "Codex status monitor",
                "progress_at": "2026-07-12T00:02:00Z",
                "handled_at": None,
                "state": "running",
                "watch_notified": True,
                "outcome": "running",
                "unrelated_alert_metadata": {"preserve": True},
            },
            {
                "id": "thread-unrelated:2026-07-12T00:01:00Z",
                "thread_id": "thread-unrelated",
                "project": "OtherProject",
                "title": "Unrelated alert",
                "progress_at": "2026-07-12T00:01:00Z",
                "handled_at": "2026-07-12T00:01:00Z",
                "state": "completed",
                "watch_notified": True,
                "outcome": "completed",
            }
        ]
        saved["unrelated_ledger_metadata"] = {
            "preserve": ["contents", "and", "formatting"]
        }
        before = json.dumps(
            saved,
            ensure_ascii=False,
            separators=(",", ":"),
        ).encode("utf-8")
        self.ledger.write_bytes(before)

        snapshot = self.tracker().refresh()
        alert = next(
            item for item in snapshot.alerts if item.thread_id == "thread-a"
        )

        self.assertIsNone(alert.turn_id)
        self.assertIsNone(alert.last_reply)
        self.assertEqual(self.ledger.read_bytes(), before)
        self.assertEqual(json.loads(self.ledger.read_bytes()), saved)

    def test_waiting_outcome_is_persisted_across_restart(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(self.rollout, "task_started", "2026-07-12T00:02:00Z")
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:02:30Z",
            last_agent_message="Please provide the missing token.",
        )

        waiting = tracker.refresh()

        self.assertEqual(waiting.alerts[0].outcome, "waiting")
        self.assertEqual(self.tracker().refresh().alerts[0].outcome, "waiting")

    def test_version_one_ledger_migrates_handled_to_completed(self):
        tracker = self.tracker()
        tracker.refresh()
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        saved["version"] = 1
        saved["alerts"] = [
            {
                "id": "thread-a:2026-07-12T00:03:00Z",
                "thread_id": "thread-a",
                "project": "StopWatch",
                "title": "Codex status monitor",
                "progress_at": "2026-07-12T00:03:00Z",
                "handled_at": "2026-07-12T00:04:00Z",
                "state": "handled",
                "watch_notified": True,
            }
        ]
        self.ledger.write_text(json.dumps(saved), encoding="utf-8")

        migrated = self.tracker().refresh().alerts[0]

        self.assertEqual(migrated.outcome, "completed")
        self.assertEqual(migrated.state, "completed")
        self.assertEqual(migrated.handled_at, "2026-07-12T00:04:00Z")
        self.assertTrue(migrated.watch_notified)
        persisted = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(persisted["version"], 3)
        self.assertEqual(persisted["turn_signals"], {})
        self.assertEqual(persisted["alerts"][0]["state"], "completed")
        self.assertEqual(persisted["alerts"][0]["outcome"], "completed")

    def test_version_two_ledger_adds_empty_turn_signals(self):
        tracker = self.tracker()
        tracker.refresh()
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        saved["version"] = 2
        saved.pop("turn_signals", None)
        self.ledger.write_text(json.dumps(saved), encoding="utf-8")

        self.tracker().refresh()

        persisted = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(persisted["version"], 3)
        self.assertEqual(persisted["turn_signals"], {})

    def test_existing_alert_synchronizes_live_project_and_title_once(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:05:00Z",
            last_agent_message="Done.",
        )
        initial = tracker.refresh()

        update_thread_metadata(
            self.state_db,
            "thread-a",
            "/workspace/StopWatch-v2",
            "添加蓝牙开关",
            2,
        )
        synchronized = tracker.refresh()

        self.assertEqual(synchronized.alerts[0].project, "StopWatch-v2")
        self.assertEqual(synchronized.alerts[0].title, "添加蓝牙开关")
        self.assertEqual(synchronized.revision, initial.revision + 1)
        self.assertEqual(tracker.refresh().revision, synchronized.revision)

    def test_codex_thread_name_overrides_full_sqlite_title(self):
        update_thread_metadata(
            self.state_db,
            "thread-a",
            "/workspace/StopWatch",
            "This is the complete first user message that should not reach the watch",
            2,
        )
        tracker = self.tracker(
            thread_name_loader=lambda: {"thread-a": "codex监控器"},
            thread_name_refresh_seconds=0,
        )
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:05:30Z",
            last_agent_message="Done.",
        )

        snapshot = tracker.refresh()

        self.assertEqual(snapshot.alerts[0].title, "codex监控器")

    def test_cached_codex_thread_name_survives_loader_failure(self):
        calls = []

        def load_names():
            calls.append(True)
            if len(calls) == 1:
                return {"thread-a": "Short title"}
            raise RuntimeError("app server unavailable")

        tracker = self.tracker(
            thread_name_loader=load_names,
            thread_name_refresh_seconds=0,
        )
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:05:45Z",
            last_agent_message="Done.",
        )

        snapshot = tracker.refresh()

        self.assertEqual(snapshot.alerts[0].title, "Short title")

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
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:03:00Z",
            last_agent_message="请回复确认。",
        )
        append_event(
            rollout_b,
            "task_complete",
            "2026-07-12T00:04:00Z",
            last_agent_message="Please confirm.",
        )
        tracker.refresh()

        append_event(self.rollout, "user_message", "2026-07-12T00:05:00Z")
        snapshot = tracker.refresh()

        by_thread = {alert.thread_id: alert for alert in snapshot.alerts}
        self.assertEqual(by_thread["thread-a"].state, "completed")
        self.assertEqual(by_thread["thread-b"].state, "pending")

    def test_snapshot_prioritizes_pending_then_running_then_completed(self):
        rollout_b = self.root / "rollout-b.jsonl"
        rollout_c = self.root / "rollout-c.jsonl"
        rollout_b.touch()
        rollout_c.touch()
        add_thread(
            self.state_db,
            "thread-b",
            "/workspace/webshot",
            "Improve UI",
            rollout_b,
            2,
        )
        add_thread(
            self.state_db,
            "thread-c",
            "/workspace/docs",
            "Update docs",
            rollout_c,
            3,
        )
        tracker = self.tracker()
        tracker.refresh()
        append_event(self.rollout, "task_started", "2026-07-12T00:01:00Z")
        append_event(
            rollout_b,
            "task_complete",
            "2026-07-12T00:02:00Z",
            last_agent_message="Please confirm.",
        )
        append_event(
            rollout_c,
            "task_complete",
            "2026-07-12T00:03:00Z",
            last_agent_message="Documentation updated.",
        )

        snapshot = tracker.refresh()

        self.assertEqual(snapshot.pending_count, 1)
        self.assertEqual(snapshot.running_count, 1)
        self.assertEqual(snapshot.completed_count, 1)
        self.assertEqual(
            [alert.state for alert in snapshot.alerts],
            ["pending", "running", "completed"],
        )

    def test_restart_resumes_saved_cursor_and_deduplicates(self):
        first = self.tracker()
        first.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:06:00Z",
            last_agent_message="Please confirm.",
        )
        self.assertEqual(first.refresh().pending_count, 1)

        restarted = self.tracker()
        self.assertEqual(restarted.refresh().pending_count, 1)
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:07:00Z",
            last_agent_message="Please confirm again.",
        )
        snapshot = restarted.refresh()

        self.assertEqual(snapshot.pending_count, 1)
        self.assertEqual(len(snapshot.alerts), 1)
        self.assertEqual(snapshot.alerts[0].progress_at, "2026-07-12T00:07:00Z")

    def test_new_thread_created_during_downtime_is_recovered(self):
        tracker = self.tracker()
        tracker.refresh()
        self.clock.value = datetime.datetime(2026, 7, 12, 0, 10, tzinfo=UTC)
        rollout_b = self.root / "new-thread.jsonl"
        append_event(
            rollout_b,
            "task_complete",
            "2026-07-12T00:09:00Z",
            last_agent_message="Please confirm.",
        )
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
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:11:00Z",
            last_agent_message="Please confirm.",
        )
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
            last_agent_message="Please confirm.",
        )
        self.assertEqual(tracker.refresh().pending_count, 0)

        with self.rollout.open("ab") as stream:
            stream.write(b"\n")

        self.assertEqual(tracker.refresh().pending_count, 1)

    def test_wait_for_change_watches_database_directory_and_rollout(self):
        tracker = self.tracker()
        tracker.refresh()

        with patch("task_alerts.wait_for_path_change", return_value=True) as wait:
            changed = tracker.wait_for_change(0.25)

        self.assertTrue(changed)
        paths, baseline, timeout = wait.call_args.args
        self.assertIn(self.root, paths)
        self.assertIn(self.state_db, paths)
        self.assertIn(self.rollout, paths)
        self.assertEqual(set(paths), set(baseline))
        self.assertEqual(timeout, 0.25)

    def test_corrupt_ledger_is_moved_aside_and_baselined(self):
        self.ledger.write_text("not json", encoding="utf-8")
        append_event(self.rollout, "task_complete", "2026-07-12T00:13:00Z")

        snapshot = self.tracker().refresh()

        self.assertEqual(snapshot.alerts, ())
        self.assertTrue(list(self.root.glob("alerts.json.corrupt-*")))

    def test_unhandled_pending_survives_midnight_but_completed_does_not(self):
        self.clock.value = datetime.datetime(2026, 7, 12, 23, 58, tzinfo=UTC)
        completed_rollout = self.root / "completed.jsonl"
        completed_rollout.touch()
        add_thread(
            self.state_db,
            "thread-completed",
            "/workspace/docs",
            "Completed task",
            completed_rollout,
            2,
        )
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T23:59:00Z",
            last_agent_message="Please confirm.",
        )
        append_event(
            completed_rollout,
            "task_complete",
            "2026-07-12T23:59:10Z",
            last_agent_message="All work is complete.",
        )
        self.assertEqual(tracker.refresh().pending_count, 1)
        self.assertEqual(tracker.refresh().completed_count, 1)

        self.clock.value = datetime.datetime(2026, 7, 13, 0, 1, tzinfo=UTC)
        next_day = tracker.refresh()

        self.assertEqual(next_day.pending_count, 1)
        self.assertEqual(next_day.completed_count, 0)
        self.assertEqual([alert.state for alert in next_day.alerts], ["pending"])
        self.assertEqual(
            tracker.pending_watch_notification_ids(),
            ("thread-a:2026-07-12T23:59:00Z",),
        )

    def test_running_survives_midnight_until_completion(self):
        self.clock.value = datetime.datetime(2026, 7, 12, 23, 58, tzinfo=UTC)
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T23:59:00Z",
            turn_id="turn-a",
        )
        self.assertEqual(tracker.refresh().running_count, 1)

        self.clock.value = datetime.datetime(2026, 7, 13, 0, 1, tzinfo=UTC)
        across_midnight = tracker.refresh()

        self.assertEqual(across_midnight.running_count, 1)
        self.assertEqual([alert.state for alert in across_midnight.alerts], ["running"])

        append_event(
            self.rollout,
            "task_complete",
            "2026-07-13T00:02:00Z",
            turn_id="turn-a",
            last_agent_message="All work is complete.",
        )
        completed = tracker.refresh()

        self.assertEqual(completed.running_count, 0)
        self.assertEqual(completed.completed_count, 1)
        self.assertEqual([alert.state for alert in completed.alerts], ["completed"])

    def test_yesterday_completion_does_not_resurrect_earlier_pending(self):
        self.clock.value = datetime.datetime(2026, 7, 12, 23, 57, tzinfo=UTC)
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T23:58:00Z",
            turn_id="turn-a",
            last_agent_message="Please confirm.",
        )
        self.assertEqual(tracker.refresh().pending_count, 1)
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T23:59:00Z",
            turn_id="turn-b",
            last_agent_message="All work is complete.",
        )
        self.assertEqual(tracker.refresh().completed_count, 1)

        self.clock.value = datetime.datetime(2026, 7, 13, 0, 1, tzinfo=UTC)
        next_day = tracker.refresh()

        self.assertEqual(next_day.running_count, 0)
        self.assertEqual(next_day.pending_count, 0)
        self.assertEqual(next_day.completed_count, 0)
        self.assertEqual(next_day.alerts, ())

    def test_alerts_older_than_seven_local_days_are_pruned(self):
        self.clock.value = datetime.datetime(2026, 7, 3, 0, 0, tzinfo=UTC)
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-03T00:01:00Z",
            last_agent_message="Please confirm.",
        )
        self.assertEqual(tracker.refresh().pending_count, 1)

        self.clock.value = datetime.datetime(2026, 7, 12, 0, 0, tzinfo=UTC)
        self.assertEqual(tracker.refresh().alerts, ())
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(saved["alerts"], [])


if __name__ == "__main__":
    unittest.main()
