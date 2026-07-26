import asyncio
import json
import pathlib
import sys
import threading
import unittest
from unittest.mock import patch


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from agent import (
    VOICE_NOTIFICATION_QUEUE_SIZE,
    VOICE_CONTROL_UUID,
    VOICE_TX_UUID,
    BleStatusTransport,
    CodexStatusAgent,
)
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


class BleStatusTransportVoiceTests(unittest.IsolatedAsyncioTestCase):
    async def test_cancelled_subscription_is_still_owned_for_notify_cleanup(self):
        class Client:
            is_connected = True

            def __init__(self):
                self.start_entered = asyncio.Event()
                self.stop_calls = 0

            async def start_notify(self, _uuid, _callback):
                self.start_entered.set()
                await asyncio.Event().wait()

            async def write_gatt_char(self, _uuid, _payload, response):
                self.response = response

            async def stop_notify(self, uuid):
                self.stop_calls += 1
                self.stopped_uuid = uuid

        client = Client()
        transport = BleStatusTransport()
        transport._client = client
        enabling = asyncio.create_task(
            transport.enable_voice(b"ready", lambda _payload: asyncio.sleep(0))
        )
        await asyncio.wait_for(client.start_entered.wait(), 1)

        enabling.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await enabling
        await transport.disable_voice(None)

        self.assertEqual(client.stop_calls, 1)
        self.assertEqual(client.stopped_uuid, VOICE_TX_UUID)

    async def test_enable_voice_subscribes_before_announcing_ready(self):
        class Client:
            is_connected = True

            def __init__(self):
                self.calls = []

            async def start_notify(self, uuid, callback):
                self.calls.append(("start_notify", uuid, callback))

            async def write_gatt_char(self, uuid, payload, response):
                self.calls.append(("write", uuid, payload, response))

            async def stop_notify(self, _uuid):
                pass

        client = Client()
        transport = BleStatusTransport()
        transport._client = client

        async def handle(_payload):
            pass

        await transport.enable_voice(b"authenticated-ready", handle)

        self.assertEqual(
            [(call[0], call[1]) for call in client.calls],
            [("start_notify", VOICE_TX_UUID), ("write", VOICE_CONTROL_UUID)],
        )
        self.assertEqual(client.calls[1][2:], (b"authenticated-ready", True))
        await transport.disable_voice(None)

    async def test_disable_voice_announces_unavailable_then_stops_once(self):
        class Client:
            is_connected = True

            def __init__(self):
                self.calls = []

            async def start_notify(self, uuid, callback):
                self.calls.append(("start_notify", uuid))

            async def write_gatt_char(self, uuid, payload, response):
                self.calls.append(("write", uuid, payload, response))

            async def stop_notify(self, uuid):
                self.calls.append(("stop_notify", uuid))

        client = Client()
        transport = BleStatusTransport()
        transport._client = client

        await transport.enable_voice(b"ready", lambda _payload: asyncio.sleep(0))
        await transport.disable_voice(b"unavailable")
        await transport.disable_voice(b"unavailable")

        self.assertEqual(
            client.calls,
            [
                ("start_notify", VOICE_TX_UUID),
                ("write", VOICE_CONTROL_UUID, b"ready", True),
                ("write", VOICE_CONTROL_UUID, b"unavailable", True),
                ("stop_notify", VOICE_TX_UUID),
            ],
        )

    async def test_notification_callback_only_copies_and_schedules_consumer(self):
        class Client:
            is_connected = True

            async def start_notify(self, _uuid, callback):
                self.callback = callback

            async def write_gatt_char(self, _uuid, _payload, response):
                self.response = response

            async def stop_notify(self, _uuid):
                pass

        client = Client()
        transport = BleStatusTransport()
        transport._client = client
        handled = []
        consumed = asyncio.Event()

        async def handle(payload):
            handled.append(payload)
            consumed.set()

        await transport.enable_voice(b"ready", handle)
        source = bytearray(b"voice-packet")

        client.callback(None, source)
        self.assertEqual(handled, [])
        source[:] = b"changed-data"
        await asyncio.wait_for(consumed.wait(), 1)

        self.assertEqual(handled, [b"voice-packet"])
        self.assertEqual(transport._voice_ingress_pending, 0)
        await transport.disable_voice(b"unavailable")

    async def test_notification_queue_overflow_fails_closed(self):
        class Client:
            is_connected = True

            async def start_notify(self, _uuid, callback):
                self.callback = callback

            async def write_gatt_char(self, _uuid, _payload, response):
                self.response = response

            async def stop_notify(self, _uuid):
                pass

        client = Client()
        transport = BleStatusTransport()
        transport._client = client
        handler_started = asyncio.Event()

        async def blocked_handler(_payload):
            handler_started.set()
            await asyncio.Event().wait()

        await transport.enable_voice(b"ready", blocked_handler)
        client.callback(None, b"active")
        await asyncio.wait_for(handler_started.wait(), 1)
        failure = transport.voice_failure

        for index in range(VOICE_NOTIFICATION_QUEUE_SIZE + 1):
            client.callback(None, bytes((index,)))
        error = await asyncio.wait_for(failure, 1)

        self.assertIsInstance(error, RuntimeError)
        self.assertEqual(str(error), "voice notification queue overflow")
        self.assertTrue(transport._voice_consumer.done())
        await transport.disable_voice(b"unavailable")

    async def test_foreign_thread_storm_bounds_work_scheduled_on_blocked_loop(self):
        class Client:
            is_connected = True

            async def start_notify(self, _uuid, callback):
                self.callback = callback

            async def write_gatt_char(self, _uuid, _payload, response):
                self.response = response

            async def stop_notify(self, _uuid):
                pass

        client = Client()
        transport = BleStatusTransport()
        transport._client = client

        async def blocked_handler(_payload):
            await asyncio.Event().wait()

        await transport.enable_voice(b"ready", blocked_handler)
        failure = transport.voice_failure
        loop = asyncio.get_running_loop()
        real_call_soon_threadsafe = loop.call_soon_threadsafe
        scheduled = []

        def count_schedule(callback, *args):
            scheduled.append(callback)
            return real_call_soon_threadsafe(callback, *args)

        def storm():
            for index in range(1000):
                client.callback(None, index.to_bytes(2, "little"))

        with patch.object(loop, "call_soon_threadsafe", side_effect=count_schedule):
            thread = threading.Thread(target=storm)
            thread.start()
            thread.join()
            self.assertLessEqual(
                len(scheduled), VOICE_NOTIFICATION_QUEUE_SIZE + 1
            )
            self.assertEqual(
                [callback.__name__ for callback in scheduled].count(
                    "_signal_voice_overflow"
                ),
                1,
            )

        error = await asyncio.wait_for(failure, 1)
        self.assertEqual(str(error), "voice notification queue overflow")
        self.assertEqual(transport._voice_ingress_pending, 0)
        await transport.disable_voice(b"unavailable")

    async def test_timed_out_unavailable_write_can_retry_notify_cleanup(self):
        class Client:
            is_connected = True

            def __init__(self):
                self.stop_calls = 0

            async def start_notify(self, _uuid, _callback):
                pass

            async def write_gatt_char(self, _uuid, payload, response):
                self.response = response
                if payload == b"unavailable":
                    await asyncio.Event().wait()

            async def stop_notify(self, _uuid):
                self.stop_calls += 1

        client = Client()
        transport = BleStatusTransport()
        transport._client = client
        await transport.enable_voice(
            b"ready", lambda _payload: asyncio.sleep(0)
        )

        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(
                transport.disable_voice(b"unavailable"), timeout=0.01
            )
        await transport.disable_voice(None)

        self.assertEqual(client.stop_calls, 1)
        self.assertIsNone(transport._voice_consumer)


if __name__ == "__main__":
    unittest.main()
