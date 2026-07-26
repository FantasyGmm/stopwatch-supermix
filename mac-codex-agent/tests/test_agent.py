import asyncio
import json
import pathlib
import sys
import unittest
from unittest.mock import patch


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from agent import (
    BleStatusTransport,
    CodexStatusAgent,
    _compact_json_bytes,
    _watch_codex_status,
    _watch_trae_status,
)
from protocol import EVENT_MESSAGE, MAX_MESSAGE_SIZE, STATUS_MESSAGE, encode_message


class FakeCollector:
    def __init__(self):
        self.calls = 0
        self.pending_ids = ("alert-a", "alert-b")
        self.notified = []

    def get_state(self):
        self.calls += 1
        return {"codex_model": "gpt-5.3", "codex_effort": "high"}

    def pending_watch_notification_ids(self):
        return self.pending_ids

    def mark_watch_notified(self, alert_ids):
        self.notified.extend(alert_ids)
        self.pending_ids = ()


class FakeTransport:
    def __init__(self, fail_event=False):
        self.messages = []
        self.fail_event = fail_event

    async def send_json(self, payload, message_type):
        if message_type == EVENT_MESSAGE and self.fail_event:
            raise ConnectionError("event write failed")
        self.messages.append((message_type, json.loads(payload)))


class Clock:
    def __init__(self, *values):
        self.values = list(values)

    def __call__(self):
        return self.values.pop(0)


def trae_status(model="Model A", detail="Building status"):
    return {
        "codex_model": "gpt-5.6-sol",
        "trae_online": True,
        "trae_model": model,
        "trae_effort": "high",
        "trae_revision": 4,
        "trae_tasks": [
            {
                "session_id": "private-session",
                "task_id": "private-task",
                "project": "StopWatch",
                "title": "Add Trae page",
                "detail": detail,
                "time": "10:00",
                "state": "running",
            }
        ],
    }


class CodexStatusAgentTests(unittest.TestCase):
    def test_sync_time_sends_current_epoch_and_timezone_offset(self):
        collector = FakeCollector()
        transport = FakeTransport()
        agent = CodexStatusAgent(
            collector=collector,
            transport=transport,
            time_ms=lambda: 1_783_872_000_123,
            utc_offset_minutes=lambda: 480,
        )

        asyncio.run(agent.sync_time())

        self.assertEqual(
            transport.messages,
            [
                (
                    EVENT_MESSAGE,
                    {
                        "type": "time_sync",
                        "unix_ms": 1_783_872_000_123,
                        "utc_offset_minutes": 480,
                    },
                )
            ],
        )

    def test_pending_alert_backlog_chimes_once_and_marks_all_notified(self):
        collector = FakeCollector()
        transport = FakeTransport()
        agent = CodexStatusAgent(collector=collector, transport=transport)

        asyncio.run(agent.tick())
        asyncio.run(agent.tick())

        status_messages = [item for item in transport.messages if item[0] == STATUS_MESSAGE]
        event_messages = [item for item in transport.messages if item[0] == EVENT_MESSAGE]
        self.assertEqual(len(status_messages), 2)
        self.assertEqual(
            event_messages,
            [(EVENT_MESSAGE, {"type": "chime", "agent": "codex"})],
        )
        self.assertEqual(collector.notified, ["alert-a", "alert-b"])

    def test_failed_chime_write_does_not_mark_alert_notified(self):
        collector = FakeCollector()
        transport = FakeTransport(fail_event=True)
        agent = CodexStatusAgent(collector=collector, transport=transport)

        with self.assertRaises(ConnectionError):
            asyncio.run(agent.tick())

        self.assertEqual(collector.notified, [])
        self.assertEqual(collector.pending_ids, ("alert-a", "alert-b"))

    def test_provided_status_is_sent_without_collecting_again(self):
        collector = FakeCollector()
        transport = FakeTransport()
        agent = CodexStatusAgent(
            collector=collector,
            transport=transport,
        )
        collector.pending_ids = ()
        status = {"codex_model": "gpt-shared", "codex_alert_pending": 0}

        result = asyncio.run(agent.tick(status=status))

        self.assertEqual(result, status)
        self.assertEqual(collector.calls, 0)
        self.assertEqual(transport.messages[0], (STATUS_MESSAGE, status))

    def test_watch_payload_limits_and_truncates_alerts(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = CodexStatusAgent(collector=collector, transport=transport)
        status = {
            "codex_alerts": [
                {
                    "project": "p" * 40,
                    "title": "t" * 60,
                    "time": "00:{:02d}".format(index),
                    "state": ("pending", "running", "completed")[index % 3],
                    "outcome": ("waiting", "running", "completed")[index % 3],
                }
                for index in range(10)
            ]
        }

        asyncio.run(agent.tick(status=status))

        sent = transport.messages[0][1]["codex_alerts"]
        self.assertEqual(len(sent), 8)
        self.assertEqual(len(sent[0]["project"]), 32)
        self.assertEqual(len(sent[0]["title"]), 48)
        self.assertEqual(sent[0]["outcome"], "waiting")
        self.assertEqual(sent[1]["state"], "running")
        self.assertEqual(sent[1]["outcome"], "running")
        self.assertEqual(sent[2]["state"], "completed")

    def test_multibyte_alerts_stay_within_ble_message_limit(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = CodexStatusAgent(collector=collector, transport=transport)
        status = {
            "codex_model": "gpt-5.6-sol",
            "codex_alerts": [
                {
                    "project": "项目" * 32,
                    "title": "进展" * 48,
                    "time": "10:00",
                    "state": "pending",
                    "outcome": "completed",
                }
                for _ in range(8)
            ],
        }

        asyncio.run(agent.tick(status=status))

        payload = transport.messages[0][1]
        encoded = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.assertLessEqual(len(encoded), 2048)
        self.assertLess(len(payload["codex_alerts"]), 8)

    def test_codex_payload_excludes_all_trae_fields(self):
        payload = _watch_codex_status(trae_status())

        self.assertEqual(payload["codex_model"], "gpt-5.6-sol")
        self.assertFalse(any(key.startswith("trae_") for key in payload))

    def test_codex_payload_keeps_legacy_shape_without_source_tag(self):
        payload = _watch_codex_status(
            {
                "codex_model": "gpt",
                "codex_effort": "high",
                "codex_fresh": True,
                "codex_collected_at": 10.0,
            }
        )

        self.assertEqual(payload, {"codex_model": "gpt", "codex_effort": "high"})
        self.assertNotIn("source", payload)

    def test_trae_payload_is_source_tagged_bounded_and_private_ids_are_removed(self):
        payload = _watch_trae_status(trae_status(detail="进展" * 200))
        encoded = json.dumps(
            payload, ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8")

        self.assertEqual(payload["type"], "status")
        self.assertEqual(payload["source"], "trae")
        self.assertLessEqual(len(encoded), 1024)
        self.assertNotIn("session_id", payload["trae_tasks"][0])
        self.assertNotIn("task_id", payload["trae_tasks"][0])
        self.assertLessEqual(len(payload["trae_tasks"][0]["detail"]), 72)

    def test_trae_payload_keeps_at_most_four_tasks(self):
        status = trae_status()
        status["trae_tasks"] = [
            {
                "project": "Project {}".format(index),
                "title": "Task {}".format(index),
                "detail": "Detail",
                "time": "10:00",
                "state": "running",
            }
            for index in range(8)
        ]

        payload = _watch_trae_status(status)

        self.assertEqual(len(payload["trae_tasks"]), 4)
        self.assertTrue(payload["trae_tasks_truncated"])

    def test_trae_payload_marks_truncation_when_byte_limit_removes_cards(self):
        status = {
            "trae_online": True,
            "trae_model": "模" * 64,
            "trae_effort": "力" * 32,
            "trae_tasks": [
                {
                    "id": "标" * 32,
                    "project": "项" * 32,
                    "title": "题" * 48,
                    "detail": "态" * 72,
                    "time": "12:34:56",
                    "state": "running",
                    "model": "模" * 64,
                    "effort": "力" * 32,
                }
                for _ in range(4)
            ],
        }

        payload = _watch_trae_status(status)

        self.assertTrue(payload["trae_tasks_truncated"])
        self.assertLess(len(payload["trae_tasks"]), 4)
        self.assertLessEqual(len(_compact_json_bytes(payload)), 1024)

    def test_trae_payload_keeps_full_counts_when_details_are_trimmed(self):
        status = trae_status()
        status.update(
            {
                "trae_process_state": "live",
                "trae_pending_count": 3,
                "trae_running_count": 4,
                "trae_tasks_truncated": True,
            }
        )
        status["trae_tasks"] = [
            {
                "id": "id-{}".format(index),
                "project": "P",
                "title": "T{}".format(index),
                "detail": "状态" * 72,
                "time": "10:00",
                "state": "pending" if index < 3 else "running",
                "model": "Doubao",
                "effort": "High",
            }
            for index in range(7)
        ]

        payload = _watch_trae_status(status)

        self.assertEqual(payload["trae_pending_count"], 3)
        self.assertEqual(payload["trae_running_count"], 4)
        self.assertTrue(payload["trae_tasks_truncated"])
        self.assertLessEqual(len(payload["trae_tasks"]), 4)
        self.assertLessEqual(len(_compact_json_bytes(payload)), 1024)

    def test_legacy_status_derives_counts_and_process_state(self):
        payload = _watch_trae_status(trae_status())

        self.assertEqual(payload["trae_process_state"], "live")
        self.assertEqual(payload["trae_pending_count"], 0)
        self.assertEqual(payload["trae_running_count"], 1)

    def test_payload_never_exposes_private_task_keys(self):
        payload = _watch_trae_status(trae_status())
        encoded = json.dumps(payload, ensure_ascii=False)

        self.assertNotIn("private-session", encoded)
        self.assertNotIn("private-task", encoded)

    def test_unicode_truncation_produces_valid_utf8_json_and_valid_ble_frames(self):
        payload = _watch_trae_status(trae_status(detail="状态🚀" * 300))
        encoded = json.dumps(
            payload, ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8")

        frames = encode_message(
            encoded,
            message_type=STATUS_MESSAGE,
            sequence=1,
            max_write_size=20,
        )

        self.assertLessEqual(len(encoded), 1024)
        self.assertLessEqual(len(encoded), MAX_MESSAGE_SIZE)
        self.assertEqual(b"".join(frame[6:] for frame in frames), encoded)

    def test_unchanged_trae_state_uses_six_second_heartbeat(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = CodexStatusAgent(
            collector=collector,
            transport=transport,
            monotonic=Clock(0.0, 2.0, 6.0),
        )
        status = trae_status()

        asyncio.run(agent.tick(status=status))
        asyncio.run(agent.tick(status=status))
        asyncio.run(agent.tick(status=status))

        trae_messages = [
            payload
            for message_type, payload in transport.messages
            if message_type == STATUS_MESSAGE and payload.get("source") == "trae"
        ]
        self.assertEqual(len(trae_messages), 2)

    def test_changed_trae_state_sends_immediately(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = CodexStatusAgent(
            collector=collector,
            transport=transport,
            monotonic=Clock(0.0, 1.0),
        )

        asyncio.run(agent.tick(status=trae_status(model="Model A")))
        asyncio.run(agent.tick(status=trae_status(model="Model B")))

        trae_messages = [
            payload
            for _, payload in transport.messages
            if payload.get("source") == "trae"
        ]
        self.assertEqual(
            [payload["trae_model"] for payload in trae_messages],
            ["Model A", "Model B"],
        )

    def test_reconnect_order_is_time_then_codex_then_trae(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = CodexStatusAgent(
            collector=collector,
            transport=transport,
            monotonic=Clock(0.0),
        )

        asyncio.run(agent.sync_time())
        asyncio.run(agent.tick(status=trae_status()))

        self.assertEqual(transport.messages[0][1]["type"], "time_sync")
        self.assertNotIn("source", transport.messages[1][1])
        self.assertEqual(transport.messages[2][1]["source"], "trae")

    def test_trae_state_never_emits_a_chime_event(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = CodexStatusAgent(
            collector=collector,
            transport=transport,
            monotonic=Clock(0.0),
        )

        asyncio.run(agent.tick(status=trae_status()))

        self.assertFalse(
            any(
                message_type == EVENT_MESSAGE and payload.get("type") == "chime"
                for message_type, payload in transport.messages
            )
        )

    def test_large_status_write_yields_before_watch_frame_queue_fills(self):
        class QueueLimitedClient:
            is_connected = True

            def __init__(self):
                self.queued = 0
                self.writes = 0

            async def write_gatt_char(self, _uuid, _frame, response):
                self.assert_response = response
                self.queued += 1
                self.writes += 1
                if self.queued > 32:
                    raise RuntimeError("watch frame queue is full")

        client = QueueLimitedClient()

        async def drain_watch_queue(_delay):
            client.queued = 0

        async def send_large_status():
            transport = BleStatusTransport()
            transport._client = client
            with patch("agent.asyncio.sleep", side_effect=drain_watch_queue) as sleep:
                await transport.send_json("x" * 1000, STATUS_MESSAGE)
                return sleep.await_count

        sleep_count = asyncio.run(send_large_status())

        self.assertGreater(client.writes, 32)
        self.assertTrue(client.assert_response)
        self.assertGreater(sleep_count, 0)


if __name__ == "__main__":
    unittest.main()
