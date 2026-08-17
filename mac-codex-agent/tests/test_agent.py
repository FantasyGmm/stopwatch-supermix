import asyncio
import json
import pathlib
import sys
import threading
import unittest
from unittest.mock import patch


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from agent import (
    PAIRING_CONTROL_UUID,
    PAIRING_TX_UUID,
    STATUS_RX_UUID,
    INPUT_CONTROL_UUID,
    INPUT_NOTIFICATION_QUEUE_SIZE,
    INPUT_TX_UUID,
    VOICE_NOTIFICATION_QUEUE_SIZE,
    VOICE_CONTROL_UUID,
    VOICE_TX_UUID,
    BleStatusTransport,
    CodexStatusAgent,
    WatchDiscovery,
    _compact_json_bytes,
    _new_task_detail_nonce,
    task_detail_messages,
    _watch_codex_status,
    _watch_trae_status,
)
from secure_pairing_wire import (
    WireMessageType,
    decode_frame as decode_pairing_frame,
    encode_discovery_response,
)
from protocol import EVENT_MESSAGE, MAX_MESSAGE_SIZE, STATUS_MESSAGE, encode_message
from task_detail_crypto import decrypt_task_detail


TEST_DETAIL_KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
TEST_DETAIL_NONCE = bytes.fromhex("1011121314151617")
TEST_DETAIL_EPOCH = 7


async def _append_async(target, value):
    target.append(value)


class FakeCollector:
    def __init__(self):
        self.calls = 0
        self.pending_ids = ("alert-a", "alert-b")
        self.notified = []

    def get_state(self):
        self.calls += 1
        return {
            "codex_model": "gpt-5.3",
            "codex_effort": "high",
            "_codex_watch_notification_ids": self.pending_ids,
        }

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


def secure_status_agent(collector, transport, nonce=TEST_DETAIL_NONCE):
    return CodexStatusAgent(
        collector=collector,
        transport=transport,
        detail_key=TEST_DETAIL_KEY,
        detail_key_epoch=TEST_DETAIL_EPOCH,
        detail_nonce_factory=lambda: nonce,
    )


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
    def test_production_task_detail_nonce_rejects_zero(self):
        expected = bytes.fromhex("2021222324252627")
        with patch(
            "agent.secrets.token_bytes",
            side_effect=(bytes(8), expected),
        ) as token_bytes:
            nonce = _new_task_detail_nonce()

        self.assertEqual(nonce, expected)
        self.assertEqual(token_bytes.call_count, 2)

    def test_default_status_agent_sessions_receive_distinct_random_nonces(self):
        first = bytes.fromhex("1011121314151617")
        second = bytes.fromhex("2021222324252627")
        with patch(
            "agent.secrets.token_bytes",
            side_effect=(first, second),
        ):
            agents = [
                CodexStatusAgent(
                    collector=FakeCollector(),
                    transport=FakeTransport(),
                    detail_key=TEST_DETAIL_KEY,
                    detail_key_epoch=TEST_DETAIL_EPOCH,
                )
                for _ in range(2)
            ]

        self.assertEqual(
            [agent._task_detail_nonce for agent in agents],
            [first, second],
        )

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

    def test_stale_snapshot_cannot_chime_or_acknowledge_twice(self):
        collector = FakeCollector()
        transport = FakeTransport()
        agent = CodexStatusAgent(collector=collector, transport=transport)
        status = collector.get_state()

        asyncio.run(agent.tick(status=status))
        asyncio.run(agent.tick(status=status))

        chimes = [
            payload
            for message_type, payload in transport.messages
            if message_type == EVENT_MESSAGE and payload.get("type") == "chime"
        ]
        self.assertEqual(chimes, [{"type": "chime", "agent": "codex"}])
        self.assertEqual(collector.notified, ["alert-a", "alert-b"])

    def test_failed_acknowledgement_retries_without_repeating_chime(self):
        class RetryCollector(FakeCollector):
            def __init__(self):
                super().__init__()
                self.mark_calls = 0

            def mark_watch_notified(self, alert_ids):
                self.mark_calls += 1
                if self.mark_calls == 1:
                    raise OSError("ledger temporarily unavailable")
                super().mark_watch_notified(alert_ids)

        collector = RetryCollector()
        transport = FakeTransport()
        agent = CodexStatusAgent(collector=collector, transport=transport)
        status = collector.get_state()

        asyncio.run(agent.tick(status=status))
        asyncio.run(agent.tick(status=status))

        chimes = [
            payload
            for message_type, payload in transport.messages
            if message_type == EVENT_MESSAGE and payload.get("type") == "chime"
        ]
        self.assertEqual(chimes, [{"type": "chime", "agent": "codex"}])
        self.assertEqual(collector.mark_calls, 2)
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

    def test_provided_status_cannot_chime_from_a_newer_live_ledger(self):
        collector = FakeCollector()

        class LedgerAdvancingTransport(FakeTransport):
            async def send_json(self, payload, message_type):
                await super().send_json(payload, message_type)
                if message_type == STATUS_MESSAGE:
                    collector.pending_ids = ("newer-alert",)

        transport = LedgerAdvancingTransport()
        agent = CodexStatusAgent(collector=collector, transport=transport)
        status = {
            "codex_alert_pending": 0,
            "codex_alert_revision": 10,
            "codex_alerts": [],
            "_codex_watch_notification_ids": (),
        }

        asyncio.run(agent.tick(status=status))

        self.assertEqual(
            transport.messages,
            [
                (
                    STATUS_MESSAGE,
                    {
                        "codex_alert_pending": 0,
                        "codex_alert_revision": 10,
                        "codex_alerts": [],
                    },
                )
            ],
        )
        self.assertEqual(collector.notified, [])

    def test_watch_payload_is_active_pending_first_and_excludes_private_fields(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = CodexStatusAgent(collector=collector, transport=transport)
        status = {
            "codex_alerts": [
                {
                    "route": "{:016x}".format(index),
                    "project": "项目" * 40,
                    "title": "进展" * 60,
                    "time": "00:00",
                    "state": state,
                    "outcome": "waiting",
                    "turn_id": "private-turn",
                    "last_reply": "private reply",
                    "thread_id": "private-thread",
                }
                for index, state in enumerate(
                    ("running", "completed", "pending", "running", "pending")
                )
            ]
        }

        asyncio.run(agent.tick(status=status))

        sent = transport.messages[0][1]["codex_alerts"]
        self.assertEqual(
            [item["state"] for item in sent],
            ["pending", "pending", "running", "running"],
        )
        self.assertEqual(
            [item["route"] for item in sent],
            [
                "0000000000000002",
                "0000000000000004",
                "0000000000000000",
                "0000000000000003",
            ],
        )
        self.assertTrue(
            all(
                set(item) == {"route", "project", "title", "state"}
                for item in sent
            )
        )
        self.assertEqual(len(sent[0]["project"]), 32)
        self.assertEqual(len(sent[0]["title"]), 48)

    def test_multibyte_alerts_stay_within_ble_message_limit(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = CodexStatusAgent(collector=collector, transport=transport)
        status = {
            "codex_model": "gpt-5.6-sol",
            "codex_alerts": [
                {
                    "route": "{:016x}".format(index),
                    "project": "项目" * 32,
                    "title": "进展" * 48,
                    "time": "10:00",
                    "state": "pending",
                    "outcome": "completed",
                }
                for index in range(8)
            ],
        }

        asyncio.run(agent.tick(status=status))

        payload = transport.messages[0][1]
        encoded = json.dumps(
            payload, ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8")
        self.assertLessEqual(len(encoded), 2048)
        self.assertLess(len(payload["codex_alerts"]), 8)

    def test_watch_payload_keeps_at_most_eight_active_tasks(self):
        payload = _watch_codex_status(
            {
                "codex_alerts": [
                    {
                        "route": "{:016x}".format(index),
                        "project": "p",
                        "title": "t",
                        "state": "running",
                    }
                    for index in range(10)
                ]
            }
        )

        self.assertEqual(len(payload["codex_alerts"]), 8)

    def test_watch_payload_does_not_forward_malformed_raw_alert_container(self):
        payload = _watch_codex_status(
            {"codex_alerts": {"last_reply": "private", "thread_id": "private"}}
        )

        self.assertNotIn("codex_alerts", payload)

    def test_watch_payload_bounds_oversize_scalar_fields_with_no_alerts(self):
        payload = _watch_codex_status(
            {
                "codex_model": "模型" * 3000,
                "codex_effort": "extremely-high" * 3000,
                "codex_5h_left_pct": 72,
                "codex_week_left_pct": 64,
                "codex_alert_pending": 123,
                "codex_alert_running": 45,
                "codex_alert_completed": 678,
                "codex_alert_revision": 901,
                "codex_alerts": [],
            }
        )

        self.assertLessEqual(len(_compact_json_bytes(payload)), MAX_MESSAGE_SIZE)
        self.assertLessEqual(len(payload["codex_model"]), 64)
        self.assertLessEqual(len(payload["codex_effort"]), 32)
        self.assertEqual(payload["codex_5h_left_pct"], 72)
        self.assertEqual(payload["codex_week_left_pct"], 64)
        self.assertEqual(payload["codex_alert_pending"], 123)
        self.assertEqual(payload["codex_alert_running"], 45)
        self.assertEqual(payload["codex_alert_completed"], 678)
        self.assertEqual(payload["codex_alert_revision"], 901)

    def test_watch_payload_fails_safe_and_keeps_pending_first_tasks_that_fit(self):
        payload = _watch_codex_status(
            {
                "codex_model": "m" * 3000,
                "codex_effort": "e" * 3000,
                "codex_alert_pending": 4,
                "codex_alert_running": 4,
                "codex_alert_revision": 8,
                "codex_alerts": [
                    {
                        "route": "{:016x}".format(index),
                        "project": "项目" * 1000,
                        "title": "任务" * 1000,
                        "state": "pending" if index % 2 else "running",
                    }
                    for index in range(8)
                ],
            }
        )

        self.assertLessEqual(len(_compact_json_bytes(payload)), MAX_MESSAGE_SIZE)
        self.assertGreater(len(payload["codex_alerts"]), 0)
        states = [task["state"] for task in payload["codex_alerts"]]
        self.assertEqual(states, sorted(states, key=lambda state: state != "pending"))

    def test_task_detail_messages_split_multibyte_text_on_character_boundaries(self):
        text = "进展🙂" * 900
        messages = task_detail_messages(
            {
                "codex_alert_revision": 7,
                "codex_alerts": [
                    {
                        "route": "0123456789abcdef",
                        "project": "p",
                        "title": "t",
                        "state": "running",
                        "last_reply": text,
                    }
                ],
            }
        )

        self.assertGreater(len(messages), 1)
        self.assertEqual("".join(message["text"] for message in messages), text)
        self.assertEqual(
            [message["index"] for message in messages], list(range(len(messages)))
        )
        self.assertTrue(
            all(message["count"] == len(messages) for message in messages)
        )
        self.assertTrue(
            all(
                len(_compact_json_bytes(message)) <= MAX_MESSAGE_SIZE
                for message in messages
            )
        )

    def test_task_detail_messages_bound_long_reply_including_explicit_marker(self):
        marker = "\n\n[内容过长，手表仅显示前 12 KiB]"
        messages = task_detail_messages(
            {
                "codex_alert_revision": 3,
                "codex_alerts": [
                    {
                        "route": "fedcba9876543210",
                        "project": "p",
                        "title": "t",
                        "state": "pending",
                        "last_reply": "🙂" * 4000,
                    }
                ],
            }
        )
        bounded = "".join(message["text"] for message in messages)
        prefix = bounded[: -len(marker)]

        self.assertTrue(bounded.endswith(marker))
        self.assertEqual(len(prefix.encode("utf-8")), 12 * 1024)
        self.assertEqual(prefix, "🙂" * 3072)
        self.assertEqual(
            len(bounded.encode("utf-8")),
            12 * 1024 + len(marker.encode("utf-8")),
        )
        self.assertTrue(
            all(
                len(_compact_json_bytes(message)) <= MAX_MESSAGE_SIZE
                for message in messages
            )
        )

    def test_task_detail_bounding_does_not_encode_the_unbounded_source(self):
        class NoWholeEncode(str):
            def encode(self, *args, **kwargs):
                raise AssertionError("unbounded source was encoded as a whole")

        source = NoWholeEncode("🙂" * 1_000_000)
        messages = task_detail_messages(
            {
                "codex_alert_revision": 3,
                "codex_alerts": [
                    {
                        "route": "fedcba9876543210",
                        "project": "p",
                        "title": "t",
                        "state": "pending",
                        "last_reply": source,
                    }
                ],
            }
        )
        bounded = "".join(message["text"] for message in messages)

        self.assertTrue(bounded.endswith("[内容过长，手表仅显示前 12 KiB]"))

    def test_task_detail_prefix_stops_before_a_split_multibyte_character(self):
        marker = "\n\n[内容过长，手表仅显示前 12 KiB]"
        source = "a" + "🙂" * 4000
        messages = task_detail_messages(
            {
                "codex_alert_revision": 3,
                "codex_alerts": [
                    {
                        "route": "fedcba9876543210",
                        "project": "p",
                        "title": "t",
                        "state": "pending",
                        "last_reply": source,
                    }
                ],
            }
        )
        bounded = "".join(message["text"] for message in messages)
        prefix = bounded[: -len(marker)]

        self.assertEqual(prefix, "a" + "🙂" * 3071)
        self.assertEqual(len(prefix.encode("utf-8")), 12 * 1024 - 3)
        self.assertTrue(source.startswith(prefix))
        self.assertTrue(bounded.endswith(marker))

    def test_task_detail_messages_emit_empty_reply(self):
        messages = task_detail_messages(
            {
                "codex_alert_revision": 5,
                "codex_alerts": [
                    {
                        "route": "0123456789abcdef",
                        "project": "p",
                        "title": "t",
                        "state": "pending",
                        "last_reply": "",
                    }
                ],
            }
        )

        self.assertEqual(
            messages,
            [
                {
                    "type": "task_detail",
                    "route": "0123456789abcdef",
                    "revision": 5,
                    "index": 0,
                    "count": 1,
                    "text": "",
                }
            ],
        )

    def test_task_detail_messages_do_not_emit_an_oversize_empty_envelope(self):
        messages = task_detail_messages(
            {
                "codex_alert_revision": 5,
                "codex_alerts": [
                    {
                        "route": "0123456789abcdef",
                        "project": "p",
                        "title": "t",
                        "state": "pending",
                        "last_reply": "",
                    }
                ],
            },
            max_size=1,
        )

        self.assertEqual(messages, [])

    def test_task_detail_messages_validate_inputs_and_fail_closed_on_collision(self):
        valid = {
            "route": "0123456789abcdef",
            "project": "p",
            "title": "t",
            "state": "running",
            "last_reply": "first",
        }
        status = {
            "codex_alert_revision": True,
            "codex_alerts": [
                None,
                {"route": "INVALID", "state": "pending", "last_reply": "bad"},
                {
                    "route": "abcdefabcdefabcd",
                    "state": "completed",
                    "last_reply": "done",
                },
                {
                    "route": "1111111111111111",
                    "state": "running",
                    "last_reply": 42,
                },
                valid,
                dict(valid, last_reply="duplicate"),
            ],
        }

        self.assertEqual(task_detail_messages(status), [])

    def test_task_detail_messages_default_each_invalid_revision_to_zero(self):
        for invalid_revision in (-1, 1.5, "7", True):
            with self.subTest(revision=invalid_revision):
                messages = task_detail_messages(
                    {
                        "codex_alert_revision": invalid_revision,
                        "codex_alerts": [
                            {
                                "route": "0123456789abcdef",
                                "project": "p",
                                "title": "t",
                                "state": "running",
                                "last_reply": "reply",
                            }
                        ],
                    }
                )

                self.assertEqual(messages[0]["revision"], 0)

    def test_task_detail_helper_uses_only_final_retained_routes_by_default(self):
        messages = task_detail_messages(
            {
                "codex_alert_revision": 3,
                "codex_alerts": [
                    {
                        "route": "{:016x}".format(index),
                        "project": "p",
                        "title": "t",
                        "state": "pending" if index % 2 else "running",
                        "last_reply": "reply-{}".format(index),
                    }
                    for index in range(10)
                ],
            }
        )

        self.assertEqual(
            {message["route"] for message in messages},
            {
                "0000000000000001",
                "0000000000000003",
                "0000000000000005",
                "0000000000000007",
                "0000000000000009",
                "0000000000000000",
                "0000000000000002",
                "0000000000000004",
            },
        )

    def test_task_details_follow_status_and_unchanged_content_is_suppressed(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = secure_status_agent(collector, transport)
        status = {
            "codex_alert_revision": 2,
            "codex_alerts": [
                {
                    "route": "0123456789abcdef",
                    "project": "p",
                    "title": "t",
                    "state": "running",
                    "last_reply": "exact",
                }
            ],
        }

        asyncio.run(agent.tick(status=status))
        asyncio.run(agent.tick(status=status))

        self.assertEqual(
            [kind for kind, _ in transport.messages],
            [STATUS_MESSAGE, STATUS_MESSAGE, STATUS_MESSAGE],
        )
        self.assertNotEqual(transport.messages[0][1].get("type"), "task_detail")
        self.assertEqual(transport.messages[1][1]["type"], "task_detail")
        self.assertNotIn("text", transport.messages[1][1])
        self.assertEqual(
            decrypt_task_detail(TEST_DETAIL_KEY, transport.messages[1][1]),
            "exact",
        )

    def test_missing_detail_credential_fails_closed_but_status_still_sends(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = CodexStatusAgent(collector=collector, transport=transport)
        status = {
            "codex_alert_revision": 2,
            "codex_alerts": [
                {
                    "route": "0123456789abcdef",
                    "project": "p",
                    "title": "t",
                    "state": "running",
                    "last_reply": "must remain private",
                }
            ],
        }

        asyncio.run(agent.tick(status=status))

        self.assertEqual(len(transport.messages), 1)
        self.assertNotIn("must remain private", json.dumps(transport.messages))

    def test_details_are_generated_only_for_routes_retained_by_main_status(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = secure_status_agent(collector, transport)
        status = {
            "codex_alert_revision": 2,
            "codex_alerts": [
                {
                    "route": "{:016x}".format(index),
                    "project": "p",
                    "title": "t",
                    "state": "pending" if index % 2 else "running",
                    "last_reply": "reply-{}".format(index),
                }
                for index in range(10)
            ],
        }

        asyncio.run(agent.tick(status=status))

        retained = {
            item["route"] for item in transport.messages[0][1]["codex_alerts"]
        }
        detail_routes = {
            payload["route"]
            for _, payload in transport.messages[1:]
            if payload.get("type") == "task_detail"
        }
        self.assertEqual(len(retained), 8)
        self.assertEqual(detail_routes, retained)

    def test_byte_budget_evicted_routes_do_not_send_or_enter_detail_cache(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = secure_status_agent(collector, transport)
        status = {
            "codex_alert_revision": 2,
            "codex_alerts": [
                {
                    "route": "{:016x}".format(index),
                    "project": "项目" * 32,
                    "title": "进展" * 48,
                    "state": "pending",
                    "last_reply": "reply-{}".format(index),
                }
                for index in range(8)
            ],
        }

        asyncio.run(agent.tick(status=status))

        retained = {
            item["route"] for item in transport.messages[0][1]["codex_alerts"]
        }
        self.assertLess(len(retained), 8)
        self.assertEqual(set(agent._task_detail_cache), retained)
        self.assertEqual(
            {
                payload["route"]
                for _, payload in transport.messages[1:]
                if payload.get("type") == "task_detail"
            },
            retained,
        )

    def test_colliding_active_routes_fail_closed_for_list_and_details(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = secure_status_agent(collector, transport)
        collision = "0123456789abcdef"
        status = {
            "codex_alert_revision": 2,
            "codex_alerts": [
                {
                    "route": collision,
                    "project": "p1",
                    "title": "t1",
                    "state": "pending",
                    "last_reply": "secret one",
                },
                {
                    "route": collision,
                    "project": "p2",
                    "title": "t2",
                    "state": "running",
                    "last_reply": "secret two",
                },
                {
                    "route": "fedcba9876543210",
                    "project": "safe",
                    "title": "safe",
                    "state": "running",
                    "last_reply": "safe reply",
                },
            ],
        }

        asyncio.run(agent.tick(status=status))

        self.assertEqual(
            [item["route"] for item in transport.messages[0][1]["codex_alerts"]],
            ["fedcba9876543210"],
        )
        details = [
            payload
            for _, payload in transport.messages
            if payload.get("type") == "task_detail"
        ]
        self.assertEqual([item["route"] for item in details], ["fedcba9876543210"])

    def test_active_and_completed_collision_stays_excluded_for_agent_lifetime(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = secure_status_agent(collector, transport)
        route = "0123456789abcdef"
        active = {
            "route": route,
            "project": "active",
            "title": "active",
            "state": "running",
            "last_reply": "must never be first wins",
        }
        completed = {
            "route": route,
            "project": "completed",
            "title": "completed",
            "state": "completed",
            "last_reply": "old",
        }

        asyncio.run(
            agent.tick(
                status={
                    "codex_alert_revision": 1,
                    "codex_alerts": [active, completed],
                }
            )
        )
        asyncio.run(
            agent.tick(
                status={
                    "codex_alert_revision": 2,
                    "codex_alerts": [active],
                }
            )
        )

        compact = [
            payload
            for _, payload in transport.messages
            if payload.get("type") != "task_detail"
        ]
        self.assertEqual(
            [payload.get("codex_alerts") for payload in compact], [[], []]
        )
        self.assertFalse(
            any(
                payload.get("type") == "task_detail"
                for _, payload in transport.messages
            )
        )

    def test_fresh_agent_can_use_route_after_authoritative_clean_snapshot(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        route = "0123456789abcdef"
        task = {
            "route": route,
            "project": "safe",
            "title": "safe",
            "state": "running",
            "last_reply": "fresh reply",
        }
        collided = dict(task, project="peer", title="peer")
        retired_transport = FakeTransport()
        retired_agent = secure_status_agent(collector, retired_transport)
        asyncio.run(
            retired_agent.tick(
                status={"codex_alert_revision": 1, "codex_alerts": [task, collided]}
            )
        )
        fresh_transport = FakeTransport()

        asyncio.run(
            secure_status_agent(collector, fresh_transport).tick(
                status={"codex_alert_revision": 2, "codex_alerts": [task]}
            )
        )

        self.assertEqual(
            fresh_transport.messages[0][1]["codex_alerts"][0]["route"],
            route,
        )
        detail = next(
            payload
            for _, payload in fresh_transport.messages
            if payload.get("type") == "task_detail"
        )
        self.assertEqual(decrypt_task_detail(TEST_DETAIL_KEY, detail), "fresh reply")

    def test_noncolliding_route_can_disappear_and_reappear(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = secure_status_agent(collector, transport)
        task = {
            "route": "0123456789abcdef",
            "project": "safe",
            "title": "safe",
            "state": "running",
            "last_reply": "reply",
        }

        asyncio.run(
            agent.tick(
                status={"codex_alert_revision": 1, "codex_alerts": [task]}
            )
        )
        asyncio.run(
            agent.tick(status={"codex_alert_revision": 2, "codex_alerts": []})
        )
        asyncio.run(
            agent.tick(
                status={"codex_alert_revision": 3, "codex_alerts": [task]}
            )
        )

        details = [
            payload
            for _, payload in transport.messages
            if payload.get("type") == "task_detail"
        ]
        self.assertEqual(
            [decrypt_task_detail(TEST_DETAIL_KEY, payload) for payload in details],
            ["reply", "reply"],
        )

    def test_nested_task_values_require_strict_utf8_strings(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = secure_status_agent(collector, transport)
        status = {
            "codex_alert_revision": 0x80000000,
            "codex_alerts": [
                {
                    "route": "0000000000000001",
                    "project": ["private-project"],
                    "title": "t",
                    "state": "pending",
                    "last_reply": "private one",
                },
                {
                    "route": "0000000000000002",
                    "project": "p",
                    "title": "t" * 48 + "\ud800",
                    "state": "pending",
                    "last_reply": "private two",
                },
                {
                    "route": "0000000000000003",
                    "project": "p",
                    "title": "t",
                    "state": "running",
                    "last_reply": {"private": "container"},
                },
                {
                    "route": "0000000000000004",
                    "project": "p",
                    "title": "t",
                    "state": "running",
                    "last_reply": "bad\ud800",
                },
            ],
        }

        asyncio.run(agent.tick(status=status))

        self.assertEqual(
            [item["route"] for item in transport.messages[0][1]["codex_alerts"]],
            ["0000000000000003", "0000000000000004"],
        )
        self.assertFalse(
            any(
                payload.get("type") == "task_detail"
                for _, payload in transport.messages
            )
        )
        self.assertEqual(transport.messages[0][1]["codex_alert_revision"], 0)

    def test_new_agent_session_resends_unchanged_task_details(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        status = {
            "codex_alert_revision": 2,
            "codex_alerts": [
                {
                    "route": "0123456789abcdef",
                    "project": "p",
                    "title": "t",
                    "state": "running",
                    "last_reply": "exact",
                }
            ],
        }

        asyncio.run(secure_status_agent(collector, transport).tick(status=status))
        asyncio.run(
            secure_status_agent(
                collector,
                transport,
                nonce=bytes.fromhex("2021222324252627"),
            ).tick(status=status)
        )

        details = [
            payload
            for _, payload in transport.messages
            if payload.get("type") == "task_detail"
        ]
        self.assertEqual(
            [decrypt_task_detail(TEST_DETAIL_KEY, message) for message in details],
            ["exact", "exact"],
        )
        self.assertNotEqual(details[0]["nonce"], details[1]["nonce"])

    def test_detail_credential_rotation_resets_nonce_sequence_and_cache(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        nonces = iter(
            (
                TEST_DETAIL_NONCE,
                bytes.fromhex("2021222324252627"),
            )
        )
        agent = CodexStatusAgent(
            collector=collector,
            transport=transport,
            detail_key=TEST_DETAIL_KEY,
            detail_key_epoch=TEST_DETAIL_EPOCH,
            detail_nonce_factory=lambda: next(nonces),
        )
        status = {
            "codex_alert_revision": 2,
            "codex_alerts": [
                {
                    "route": "0123456789abcdef",
                    "project": "p",
                    "title": "t",
                    "state": "running",
                    "last_reply": "exact",
                }
            ],
        }

        asyncio.run(agent.tick(status=status))
        agent.update_detail_credential(b"N" * 16, TEST_DETAIL_EPOCH + 1)
        asyncio.run(agent.tick(status=status))

        details = [
            payload
            for _, payload in transport.messages
            if payload.get("type") == "task_detail"
        ]
        self.assertEqual([item["sequence"] for item in details], [1, 1])
        self.assertNotEqual(details[0]["nonce"], details[1]["nonce"])
        self.assertEqual(decrypt_task_detail(TEST_DETAIL_KEY, details[0]), "exact")
        self.assertEqual(decrypt_task_detail(b"N" * 16, details[1]), "exact")

    def test_detail_credential_rotation_never_reuses_a_session_nonce(self):
        collector = FakeCollector()
        transport = FakeTransport()
        replacement = bytes.fromhex("2021222324252627")
        nonces = iter((TEST_DETAIL_NONCE, TEST_DETAIL_NONCE, replacement))
        agent = CodexStatusAgent(
            collector=collector,
            transport=transport,
            detail_key=TEST_DETAIL_KEY,
            detail_key_epoch=TEST_DETAIL_EPOCH,
            detail_nonce_factory=lambda: next(nonces),
        )

        agent.update_detail_credential(b"N" * 16, TEST_DETAIL_EPOCH + 1)

        self.assertEqual(agent._task_detail_nonce, replacement)

    def test_detail_reenable_with_same_credential_uses_fresh_nonce_and_sequence(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        replacement = bytes.fromhex("2021222324252627")
        nonces = iter((TEST_DETAIL_NONCE, replacement))
        agent = CodexStatusAgent(
            collector=collector,
            transport=transport,
            detail_key=TEST_DETAIL_KEY,
            detail_key_epoch=TEST_DETAIL_EPOCH,
            detail_nonce_factory=lambda: next(nonces),
        )
        status = {
            "codex_alert_revision": 2,
            "codex_alerts": [
                {
                    "route": "0123456789abcdef",
                    "project": "p",
                    "title": "t",
                    "state": "running",
                    "last_reply": "exact",
                }
            ],
        }

        asyncio.run(agent.tick(status=status))
        agent.disable_detail_credential()
        asyncio.run(agent.tick(status=status))
        agent.update_detail_credential(TEST_DETAIL_KEY, TEST_DETAIL_EPOCH)
        asyncio.run(agent.tick(status=status))

        details = [
            payload
            for _, payload in transport.messages
            if payload.get("type") == "task_detail"
        ]
        self.assertEqual([item["sequence"] for item in details], [1, 1])
        self.assertEqual(
            [bytes.fromhex(item["nonce"]) for item in details],
            [TEST_DETAIL_NONCE, replacement],
        )

    def test_task_detail_change_resends_and_disappearance_allows_reappearance(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = secure_status_agent(collector, transport)
        task = {
            "route": "0123456789abcdef",
            "project": "p",
            "title": "t",
            "state": "running",
            "last_reply": "one",
        }

        asyncio.run(
            agent.tick(status={"codex_alert_revision": 1, "codex_alerts": [task]})
        )
        asyncio.run(
            agent.tick(
                status={
                    "codex_alert_revision": 1,
                    "codex_alerts": [dict(task, last_reply="two")],
                }
            )
        )
        asyncio.run(
            agent.tick(
                status={
                    "codex_alert_revision": 2,
                    "codex_alerts": [dict(task, last_reply="two")],
                }
            )
        )
        asyncio.run(
            agent.tick(status={"codex_alert_revision": 3, "codex_alerts": []})
        )
        asyncio.run(
            agent.tick(
                status={
                    "codex_alert_revision": 2,
                    "codex_alerts": [dict(task, last_reply="two")],
                }
            )
        )

        details = [
            payload
            for _, payload in transport.messages
            if payload.get("type") == "task_detail"
        ]
        self.assertEqual(
            [
                (item["revision"], decrypt_task_detail(TEST_DETAIL_KEY, item))
                for item in details
            ],
            [(1, "one"), (1, "two"), (2, "two"), (2, "two")],
        )

    def test_failed_detail_set_retries_from_first_chunk(self):
        class FailSecondDetailOnce(FakeTransport):
            def __init__(self):
                super().__init__()
                self.detail_calls = 0

            async def send_json(self, payload, message_type):
                parsed = json.loads(payload)
                if parsed.get("type") == "task_detail":
                    self.detail_calls += 1
                    if self.detail_calls == 2:
                        raise ConnectionError("detail write failed")
                self.messages.append((message_type, parsed))

        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FailSecondDetailOnce()
        agent = secure_status_agent(collector, transport)
        status = {
            "codex_alert_revision": 8,
            "codex_alerts": [
                {
                    "route": "0123456789abcdef",
                    "project": "p",
                    "title": "t",
                    "state": "running",
                    "last_reply": "x" * 5000,
                }
            ],
        }

        with self.assertRaises(ConnectionError):
            asyncio.run(agent.tick(status=status))
        asyncio.run(agent.tick(status=status))
        asyncio.run(agent.tick(status=status))

        detail_indexes = [
            payload["index"]
            for _, payload in transport.messages
            if payload.get("type") == "task_detail"
        ]
        complete_set = task_detail_messages(status)
        self.assertGreater(len(complete_set), 1)
        self.assertEqual(
            detail_indexes,
            [0] + list(range(len(complete_set))),
        )
        sequences = [
            payload["sequence"]
            for _, payload in transport.messages
            if payload.get("type") == "task_detail"
        ]
        self.assertEqual(sequences, [1] + list(range(3, 3 + len(complete_set))))
        encrypted = [
            payload
            for _, payload in transport.messages
            if payload.get("type") == "task_detail"
        ]
        self.assertNotEqual(encrypted[0]["ciphertext"], encrypted[1]["ciphertext"])
        self.assertTrue(
            all(
                len(_compact_json_bytes(payload)) <= MAX_MESSAGE_SIZE
                for payload in encrypted
            )
        )
        retried = [
            payload for payload in encrypted[1:]
        ]
        self.assertEqual(
            "".join(decrypt_task_detail(TEST_DETAIL_KEY, item) for item in retried),
            "x" * 5000,
        )

    def test_detail_sequence_exhaustion_fails_closed_after_main_status(self):
        collector = FakeCollector()
        collector.pending_ids = ()
        transport = FakeTransport()
        agent = secure_status_agent(collector, transport)
        agent._task_detail_sequence = 0xFFFFFFFF
        status = {
            "codex_alert_revision": 1,
            "codex_alerts": [
                {
                    "route": "0123456789abcdef",
                    "project": "p",
                    "title": "t",
                    "state": "running",
                    "last_reply": "secret",
                }
            ],
        }

        with self.assertRaises(OverflowError):
            asyncio.run(agent.tick(status=status))

        self.assertEqual(len(transport.messages), 1)

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

    def test_trae_payload_marks_truncation_when_one_card_detail_is_shortened(self):
        status = {
            "trae_online": True,
            "trae_tasks": [
                {
                    "id": "标" * 32,
                    "project": "项" * 32,
                    "title": "题" * 48,
                    "detail": "态" * 72,
                    "time": "12:34:56",
                    "state": "running",
                    "model": "模" * 64,
                }
            ],
        }

        payload = _watch_trae_status(status)

        self.assertEqual(len(payload["trae_tasks"]), 1)
        self.assertLess(len(payload["trae_tasks"][0]["detail"]), 72)
        self.assertTrue(payload["trae_tasks_truncated"])
        self.assertLessEqual(len(_compact_json_bytes(payload)), 1024)

    def test_trae_payload_bounds_explicit_aggregate_counts_to_uint16(self):
        cases = (
            ("negative", -1, -1, 0, 0),
            ("bool", True, True, 0, 0),
            ("huge", 10**2000, 10**2000, 65535, 65535),
        )

        for case, pending, running, expected_pending, expected_running in cases:
            with self.subTest(case=case):
                payload = _watch_trae_status(
                    {
                        "trae_pending_count": pending,
                        "trae_running_count": running,
                    }
                )

                self.assertEqual(payload["trae_pending_count"], expected_pending)
                self.assertEqual(payload["trae_running_count"], expected_running)
                self.assertLessEqual(len(_compact_json_bytes(payload)), 1024)

    def test_trae_payload_bounds_huge_revision_to_firmware_signed_int(self):
        payload = _watch_trae_status({"trae_revision": 10**2000})

        self.assertEqual(payload["trae_revision"], 2147483647)
        self.assertLessEqual(len(_compact_json_bytes(payload)), 1024)

    def test_trae_payload_rejects_nonfinite_scalar_values(self):
        for case, value in (
            ("nan", float("nan")),
            ("inf", float("inf")),
            ("negative_inf", float("-inf")),
        ):
            with self.subTest(case=case):
                payload = _watch_trae_status(
                    {
                        "trae_pending_count": value,
                        "trae_running_count": value,
                        "trae_revision": value,
                    }
                )

                self.assertEqual(payload["trae_pending_count"], 0)
                self.assertEqual(payload["trae_running_count"], 0)
                self.assertEqual(payload["trae_revision"], 0)
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

    def test_status_write_uses_negotiated_characteristic_capacity(self):
        class Characteristic:
            max_write_without_response_size = 244

        class Services:
            def get_characteristic(self, uuid):
                self.requested_uuid = uuid
                return Characteristic()

        class Client:
            is_connected = True

            def __init__(self):
                self.services = Services()
                self.frames = []

            async def write_gatt_char(self, _uuid, frame, response):
                self.frames.append((bytes(frame), response))

        async def send_status():
            client = Client()
            transport = BleStatusTransport()
            transport._client = client
            await transport.send_json("x" * 1000, STATUS_MESSAGE)
            return client

        client = asyncio.run(send_status())

        self.assertEqual(len(client.frames), 5)
        self.assertEqual(client.services.requested_uuid, STATUS_RX_UUID)
        self.assertTrue(all(len(frame) <= 244 for frame, _ in client.frames))
        self.assertTrue(all(response for _, response in client.frames))

    def test_status_write_bounds_invalid_and_oversized_capacities(self):
        class Characteristic:
            def __init__(self, capacity):
                self.max_write_without_response_size = capacity

        class Services:
            def __init__(self, capacity):
                self.capacity = capacity

            def get_characteristic(self, _uuid):
                return (
                    None
                    if self.capacity is None
                    else Characteristic(self.capacity)
                )

        class Client:
            is_connected = True

            def __init__(self, capacity):
                self.services = Services(capacity)
                self.frames = []

            async def write_gatt_char(self, _uuid, frame, response):
                self.frames.append(bytes(frame))

        for capacity, expected_max in (
            (None, 20),
            (-1, 20),
            ("244", 20),
            (999, 256),
        ):
            with self.subTest(capacity=capacity):
                async def send_status():
                    client = Client(capacity)
                    transport = BleStatusTransport()
                    transport._client = client
                    await transport.send_json("x" * 1000, STATUS_MESSAGE)
                    return client.frames

                frames = asyncio.run(send_status())

                self.assertLessEqual(
                    max(len(frame) for frame in frames),
                    expected_max,
                )
                if expected_max == 20:
                    self.assertGreater(len(frames), 32)
                else:
                    self.assertLess(len(frames), 8)


class BleStatusTransportVoiceTests(unittest.IsolatedAsyncioTestCase):
    async def test_watch_discovery_uses_only_restricted_pairing_gatt(self):
        watch_id = bytes(range(16))
        watch_identity_public = b"\x02" + b"W" * 32

        class Client:
            is_connected = True

            def __init__(self):
                self.calls = []
                self.callback = None

            async def start_notify(self, uuid, callback):
                self.calls.append(("notify", uuid))
                self.callback = callback

            async def write_gatt_char(self, uuid, frame, response):
                self.calls.append(("write", uuid, response))
                kind, transaction_id, _ = decode_pairing_frame(frame)
                self.assert_kind = kind
                asyncio.get_running_loop().call_soon(
                    self.callback,
                    None,
                    encode_discovery_response(
                        transaction_id,
                        watch_id,
                        7,
                        watch_identity_public,
                    ),
                )

        client = Client()
        transport = BleStatusTransport()
        transport._client = client
        await transport.start_pairing_notifications()

        discovery = await transport.discover_watch()

        self.assertEqual(
            discovery,
            WatchDiscovery(watch_id, 7, watch_identity_public),
        )
        self.assertIs(client.assert_kind, WireMessageType.DISCOVERY_REQUEST)
        self.assertEqual(
            client.calls,
            [
                ("notify", PAIRING_TX_UUID),
                ("write", PAIRING_CONTROL_UUID, True),
            ],
        )
    async def test_status_write_times_out_when_corebluetooth_never_completes(self):
        class Client:
            is_connected = True

            async def write_gatt_char(self, _uuid, _frame, response):
                self.response = response
                await asyncio.Future()

        client = Client()
        transport = BleStatusTransport()
        transport._client = client
        started = asyncio.get_running_loop().time()

        with patch("agent.GATT_OPERATION_TIMEOUT_SECONDS", 0.01):
            with self.assertRaises(asyncio.TimeoutError):
                await asyncio.wait_for(
                    transport.send_json("status", STATUS_MESSAGE), timeout=0.15
                )

        self.assertLess(asyncio.get_running_loop().time() - started, 0.1)
        self.assertTrue(client.response)

    async def test_start_notify_failure_is_clean_before_direct_retry(self):
        class Client:
            is_connected = True

            def __init__(self):
                self.start_calls = 0
                self.stop_calls = 0
                self.callbacks = []
                self.writes = []

            async def start_notify(self, _uuid, callback):
                self.start_calls += 1
                self.callbacks.append(callback)
                if self.start_calls == 1:
                    raise RuntimeError("subscribe failed")

            async def write_gatt_char(self, _uuid, payload, response):
                self.writes.append((payload, response))

            async def stop_notify(self, _uuid):
                self.stop_calls += 1

        client = Client()
        transport = BleStatusTransport()
        transport._client = client

        with self.assertRaisesRegex(RuntimeError, "subscribe failed"):
            await transport.enable_voice(
                b"ready-1", lambda _payload: asyncio.sleep(0)
            )
        old_callback = client.callbacks[0]
        self.assertIsNone(transport.voice_failure)
        self.assertEqual(
            [
                task
                for task in asyncio.all_tasks()
                if task is not asyncio.current_task() and not task.done()
            ],
            [],
        )

        handled = []
        await transport.enable_voice(
            b"ready-2", lambda payload: _append_async(handled, payload)
        )
        old_callback(None, b"stale")
        client.callbacks[-1](None, b"current")
        await asyncio.sleep(0)
        await asyncio.sleep(0)
        await transport.disable_voice(None)

        self.assertEqual(handled, [b"current"])
        self.assertEqual(client.start_calls, 2)
        self.assertEqual(client.stop_calls, 2)
        self.assertEqual(client.writes, [(b"ready-2", True)])

    async def test_ready_write_failure_is_clean_before_direct_retry(self):
        class Client:
            is_connected = True

            def __init__(self):
                self.stop_calls = 0
                self.callbacks = []
                self.writes = []

            async def start_notify(self, _uuid, callback):
                self.callbacks.append(callback)

            async def write_gatt_char(self, _uuid, payload, response):
                self.writes.append((payload, response))
                if payload == b"ready-1":
                    raise RuntimeError("READY failed")

            async def stop_notify(self, _uuid):
                self.stop_calls += 1

        client = Client()
        transport = BleStatusTransport()
        transport._client = client

        with self.assertRaisesRegex(RuntimeError, "READY failed"):
            await transport.enable_voice(
                b"ready-1", lambda _payload: asyncio.sleep(0)
            )
        old_callback = client.callbacks[0]
        self.assertIsNone(transport.voice_failure)
        self.assertEqual(
            [
                task
                for task in asyncio.all_tasks()
                if task is not asyncio.current_task() and not task.done()
            ],
            [],
        )

        handled = []
        await transport.enable_voice(
            b"ready-2", lambda payload: _append_async(handled, payload)
        )
        old_callback(None, b"stale")
        client.callbacks[-1](None, b"current")
        await asyncio.sleep(0)
        await asyncio.sleep(0)
        await transport.disable_voice(None)

        self.assertEqual(handled, [b"current"])
        self.assertEqual(client.stop_calls, 2)
        self.assertEqual(
            client.writes,
            [(b"ready-1", True), (b"ready-2", True)],
        )

    async def test_old_notification_callback_cannot_reach_reenabled_handler(self):
        class Client:
            is_connected = True

            def __init__(self):
                self.callbacks = []

            async def start_notify(self, _uuid, callback):
                self.callbacks.append(callback)

            async def write_gatt_char(self, _uuid, _payload, response):
                self.response = response

            async def stop_notify(self, _uuid):
                pass

        client = Client()
        transport = BleStatusTransport()
        transport._client = client
        old_handled = []
        new_handled = []
        new_payload = asyncio.Event()

        await transport.enable_voice(
            b"ready-1", lambda payload: _append_async(old_handled, payload)
        )
        old_callback = client.callbacks[-1]
        await transport.disable_voice(None)

        async def handle_new(payload):
            new_handled.append(payload)
            new_payload.set()

        await transport.enable_voice(b"ready-2", handle_new)
        old_callback(None, b"retired")
        await asyncio.sleep(0)
        await asyncio.sleep(0)
        self.assertEqual(new_handled, [])

        client.callbacks[-1](None, b"current")
        await asyncio.wait_for(new_payload.wait(), 1)
        self.assertEqual(new_handled, [b"current"])
        await transport.disable_voice(None)

    async def test_delayed_old_payload_and_overflow_handles_cannot_fail_new_generation(self):
        class Client:
            is_connected = True

            def __init__(self):
                self.callbacks = []

            async def start_notify(self, _uuid, callback):
                self.callbacks.append(callback)

            async def write_gatt_char(self, _uuid, _payload, response):
                self.response = response

            async def stop_notify(self, _uuid):
                pass

        client = Client()
        transport = BleStatusTransport()
        transport._client = client
        await transport.enable_voice(
            b"ready-1", lambda _payload: asyncio.sleep(0)
        )
        old_callback = client.callbacks[-1]
        delayed = []
        loop = asyncio.get_running_loop()

        def delay(callback, *args):
            delayed.append((callback, args))

        with patch.object(loop, "call_soon_threadsafe", side_effect=delay):
            for index in range(VOICE_NOTIFICATION_QUEUE_SIZE + 1):
                old_callback(None, index.to_bytes(2, "little"))

        await transport.disable_voice(None)
        new_handled = []
        await transport.enable_voice(
            b"ready-2", lambda payload: _append_async(new_handled, payload)
        )
        new_failure = transport.voice_failure

        for callback, args in delayed:
            callback(*args)
        await asyncio.sleep(0)
        await asyncio.sleep(0)

        self.assertEqual(new_handled, [])
        self.assertFalse(new_failure.done())
        await transport.disable_voice(None)

    async def test_delayed_old_consumer_done_cannot_fail_new_generation(self):
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
        await transport.enable_voice(
            b"ready-1", lambda _payload: asyncio.sleep(0)
        )
        old_generation = transport._voice_generation
        await transport.disable_voice(None)
        await transport.enable_voice(
            b"ready-2", lambda _payload: asyncio.sleep(0)
        )
        new_generation = transport._voice_generation

        async def fail_old_consumer():
            raise RuntimeError("retired consumer failed")

        retired_task = asyncio.create_task(fail_old_consumer())
        await asyncio.sleep(0)
        transport._voice_consumer_done(old_generation, retired_task)

        self.assertTrue(old_generation.failure.done())
        self.assertFalse(new_generation.failure.done())
        self.assertFalse(new_generation.consumer.done())
        await transport.disable_voice(None)

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

    async def test_ten_second_startup_burst_is_buffered_in_order(self):
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
        startup_release = asyncio.Event()
        handler_started = asyncio.Event()
        all_handled = asyncio.Event()
        startup_frames = [index.to_bytes(2, "little") for index in range(500)]
        handled = []

        async def handle(payload):
            if payload == b"begin":
                handler_started.set()
                await startup_release.wait()
            handled.append(payload)
            if len(handled) == len(startup_frames) + 1:
                all_handled.set()

        await transport.enable_voice(b"ready", handle)
        client.callback(None, b"begin")
        await asyncio.wait_for(handler_started.wait(), 1)

        thread = threading.Thread(
            target=lambda: [client.callback(None, frame) for frame in startup_frames]
        )
        thread.start()
        thread.join()
        await asyncio.sleep(0)

        self.assertFalse(transport.voice_failure.done())
        startup_release.set()
        await asyncio.wait_for(all_handled.wait(), 1)
        self.assertEqual(handled, [b"begin", *startup_frames])
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
            client.callback(None, index.to_bytes(2, "little"))
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


class BleStatusTransportInputActionTests(unittest.IsolatedAsyncioTestCase):
    async def test_input_ready_failure_is_clean_before_direct_retry(self):
        class Client:
            is_connected = True

            def __init__(self):
                self.callbacks = []
                self.stop_calls = 0

            async def start_notify(self, _uuid, callback):
                self.callbacks.append(callback)

            async def write_gatt_char(self, _uuid, payload, response):
                self.response = response
                if payload == b"ready-1":
                    raise RuntimeError("input READY failed")

            async def stop_notify(self, _uuid):
                self.stop_calls += 1

        client = Client()
        transport = BleStatusTransport()
        transport._client = client

        with self.assertRaisesRegex(RuntimeError, "input READY failed"):
            await transport.enable_input_actions(
                b"ready-1", lambda _payload: asyncio.sleep(0)
            )
        old_callback = client.callbacks[0]
        handled = []
        await transport.enable_input_actions(
            b"ready-2", lambda payload: _append_async(handled, payload)
        )
        old_callback(None, b"stale")
        client.callbacks[-1](None, b"current")
        await asyncio.sleep(0)
        await asyncio.sleep(0)
        await transport.disable_input_actions()

        self.assertEqual(handled, [b"current"])
        self.assertEqual(client.stop_calls, 2)

    async def test_input_subscription_is_independent_and_announces_ready_after_subscribe(self):
        class Client:
            is_connected = True

            def __init__(self):
                self.calls = []

            async def start_notify(self, uuid, callback):
                self.calls.append(("start_notify", uuid, callback))

            async def write_gatt_char(self, uuid, payload, response):
                self.calls.append(("write", uuid, payload, response))

            async def stop_notify(self, uuid):
                self.calls.append(("stop_notify", uuid))

        client = Client()
        transport = BleStatusTransport()
        transport._client = client

        await transport.enable_input_actions(
            b"authenticated-input-ready", lambda _payload: asyncio.sleep(0)
        )
        await transport.disable_voice(None)
        await transport.disable_input_actions()
        await transport.disable_input_actions()

        self.assertEqual(
            [(call[0], call[1]) for call in client.calls],
            [
                ("start_notify", INPUT_TX_UUID),
                ("write", INPUT_CONTROL_UUID),
                ("stop_notify", INPUT_TX_UUID),
            ],
        )
        self.assertEqual(
            client.calls[1][2:], (b"authenticated-input-ready", True)
        )

    async def test_input_queue_drops_newest_at_eight_and_isolates_handler_errors(self):
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
        release = asyncio.Event()
        first_started = asyncio.Event()
        handled = []

        async def handle(payload):
            handled.append(payload)
            if payload == b"0":
                first_started.set()
                await release.wait()
                raise RuntimeError("desktop dispatch failed")

        await transport.enable_input_actions(b"ready", handle)
        client.callback(None, bytearray(b"0"))
        await asyncio.wait_for(first_started.wait(), 1)
        for index in range(1, INPUT_NOTIFICATION_QUEUE_SIZE + 2):
            client.callback(None, str(index).encode("ascii"))
        await asyncio.sleep(0)

        self.assertEqual(transport._input_ingress_pending, 8)
        release.set()
        for _ in range(100):
            if transport._input_ingress_pending == 0:
                break
            await asyncio.sleep(0.01)
        else:
            self.fail("input queue did not drain")

        self.assertEqual(
            handled,
            [str(index).encode("ascii") for index in range(8)],
        )
        self.assertFalse(transport._input_consumer.done())
        await transport.disable_input_actions()

    async def test_disable_retires_callback_and_can_retry_timed_out_stop(self):
        class Client:
            is_connected = True

            def __init__(self):
                self.callbacks = []
                self.stop_calls = 0

            async def start_notify(self, _uuid, callback):
                self.callbacks.append(callback)

            async def write_gatt_char(self, _uuid, _payload, response):
                self.response = response

            async def stop_notify(self, _uuid):
                self.stop_calls += 1
                if self.stop_calls == 1:
                    await asyncio.Event().wait()

        client = Client()
        transport = BleStatusTransport()
        transport._client = client
        handled = []
        await transport.enable_input_actions(
            b"ready", lambda payload: _append_async(handled, payload)
        )
        old_callback = client.callbacks[0]

        with patch("agent.GATT_OPERATION_TIMEOUT_SECONDS", 0.01):
            with self.assertRaises(asyncio.TimeoutError):
                await transport.disable_input_actions()
        old_callback(None, b"stale")
        await asyncio.sleep(0)
        await transport.disable_input_actions()

        self.assertEqual(handled, [])
        self.assertEqual(client.stop_calls, 2)
        self.assertIsNone(transport._input_consumer)


if __name__ == "__main__":
    unittest.main()
