import asyncio
import json
import pathlib
import sys
import unittest
from unittest.mock import patch


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from agent import BleStatusTransport, CodexStatusAgent
from protocol import EVENT_MESSAGE, STATUS_MESSAGE


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


class CodexStatusAgentTests(unittest.TestCase):
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
