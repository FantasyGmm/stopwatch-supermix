import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from codex_turn_evidence import (
    RolloutReplayError,
    latest_unsuperseded_completion,
    new_turn_signal,
    reduce_turn_signal,
)


class CodexTurnEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.rollout = pathlib.Path(self.temporary.name) / "rollout.jsonl"
        self.rollout.touch()

    def tearDown(self):
        self.temporary.cleanup()

    def append(self, timestamp, event_type, **payload):
        item = {
            "timestamp": timestamp,
            "type": (
                "response_item"
                if event_type == "response_item"
                else "event_msg"
            ),
            "payload": payload,
        }
        if event_type != "response_item":
            item["payload"]["type"] = event_type
        with self.rollout.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(item) + "\n")

    def test_plan_and_input_calls_share_one_reducer(self):
        signal = new_turn_signal("turn-a")
        plan_call = {
            "type": "function_call",
            "name": "update_plan",
            "arguments": json.dumps(
                {"plan": [{"step": "Choose", "status": "in_progress"}]}
            ),
            "call_id": "plan-a",
        }
        input_call = {
            "type": "function_call",
            "name": "request_user_input",
            "arguments": "{}",
            "call_id": "input-a",
        }
        input_output = {
            "type": "function_call_output",
            "call_id": "input-a",
        }

        self.assertTrue(reduce_turn_signal(signal, plan_call))
        self.assertTrue(reduce_turn_signal(signal, input_call))
        self.assertTrue(signal["unfinished_plan"])
        self.assertEqual(signal["input_calls"], ["input-a"])
        self.assertTrue(reduce_turn_signal(signal, input_output))
        self.assertEqual(signal["input_calls"], [])

    def test_latest_completion_reconstructs_structured_signals(self):
        rows = (
            {
                "timestamp": "2026-07-29T13:00:00Z",
                "type": "event_msg",
                "payload": {"type": "task_started", "turn_id": "turn-a"},
            },
            {
                "timestamp": "2026-07-29T13:00:01Z",
                "type": "response_item",
                "payload": {
                    "type": "function_call",
                    "name": "update_plan",
                    "arguments": json.dumps(
                        {"plan": [{"step": "Choose", "status": "pending"}]}
                    ),
                    "call_id": "plan-a",
                    "internal_chat_message_metadata_passthrough": {
                        "turn_id": "turn-a"
                    },
                },
            },
            {
                "timestamp": "2026-07-29T13:00:02Z",
                "type": "response_item",
                "payload": {
                    "type": "function_call",
                    "name": "request_user_input",
                    "arguments": "{}",
                    "call_id": "input-a",
                    "internal_chat_message_metadata_passthrough": {
                        "turn_id": "turn-a"
                    },
                },
            },
            {
                "timestamp": "2026-07-29T13:00:03Z",
                "type": "event_msg",
                "payload": {
                    "type": "task_complete",
                    "turn_id": "turn-a",
                    "last_agent_message": "Choose one.",
                },
            },
        )
        self.rollout.write_text(
            "".join(json.dumps(row) + "\n" for row in rows),
            encoding="utf-8",
        )

        result = latest_unsuperseded_completion(self.rollout)

        self.assertIsNotNone(result)
        self.assertEqual(result.turn_id, "turn-a")
        self.assertEqual(result.timestamp.isoformat(), "2026-07-29T13:00:03+00:00")
        self.assertEqual(result.last_agent_message, "Choose one.")
        self.assertTrue(result.unresolved_input)
        self.assertTrue(result.unfinished_plan)

    def test_function_output_resolves_input_before_completion(self):
        signal_metadata = {
            "internal_chat_message_metadata_passthrough": {"turn_id": "turn-a"}
        }
        self.append("2026-07-29T13:00:00Z", "task_started", turn_id="turn-a")
        self.append(
            "2026-07-29T13:00:01Z",
            "response_item",
            type="function_call",
            name="request_user_input",
            arguments="{}",
            call_id="input-a",
            **signal_metadata,
        )
        self.append(
            "2026-07-29T13:00:02Z",
            "response_item",
            type="function_call_output",
            call_id="input-a",
            **signal_metadata,
        )
        self.append(
            "2026-07-29T13:00:03Z",
            "task_complete",
            turn_id="turn-a",
            last_agent_message="Done.",
        )

        result = latest_unsuperseded_completion(self.rollout)

        self.assertIsNotNone(result)
        self.assertFalse(result.unresolved_input)

    def test_later_user_message_supersedes_completion(self):
        self.append("2026-07-29T13:00:00Z", "task_started", turn_id="turn-a")
        self.append(
            "2026-07-29T13:00:01Z",
            "task_complete",
            turn_id="turn-a",
            last_agent_message="Please confirm.",
        )
        self.append("2026-07-29T13:00:02Z", "user_message")

        self.assertIsNone(latest_unsuperseded_completion(self.rollout))

    def test_later_task_started_supersedes_completion(self):
        self.append("2026-07-29T13:00:00Z", "task_started", turn_id="turn-a")
        self.append(
            "2026-07-29T13:00:01Z",
            "task_complete",
            turn_id="turn-a",
            last_agent_message="Please confirm.",
        )
        self.append("2026-07-29T13:00:02Z", "task_started", turn_id="turn-b")

        self.assertIsNone(latest_unsuperseded_completion(self.rollout))

    def test_incomplete_trailing_record_is_retryable(self):
        self.rollout.write_bytes(b'{"timestamp":"2026-07-29T13:00:00Z"')

        with self.assertRaises(RolloutReplayError) as raised:
            latest_unsuperseded_completion(self.rollout)

        self.assertTrue(raised.exception.retryable)

    def test_invalid_complete_record_is_not_retryable(self):
        self.rollout.write_bytes(b"not-json\n")

        with self.assertRaises(RolloutReplayError) as raised:
            latest_unsuperseded_completion(self.rollout)

        self.assertFalse(raised.exception.retryable)

    def test_unreadable_rollout_is_not_retryable(self):
        with self.assertRaises(RolloutReplayError) as raised:
            latest_unsuperseded_completion(pathlib.Path(self.temporary.name))

        self.assertFalse(raised.exception.retryable)


if __name__ == "__main__":
    unittest.main()
