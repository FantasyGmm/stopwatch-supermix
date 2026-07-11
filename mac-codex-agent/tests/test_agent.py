import asyncio
import json
import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from agent import CodexStatusAgent
from protocol import EVENT_MESSAGE, STATUS_MESSAGE


class FakeCollector:
    def __init__(self):
        self.calls = 0

    def get_state(self):
        self.calls += 1
        return {"codex_model": "gpt-5.3", "codex_effort": "high"}


class FakeTransport:
    def __init__(self):
        self.messages = []

    async def send_json(self, payload, message_type):
        self.messages.append((message_type, json.loads(payload)))


class CodexStatusAgentTests(unittest.TestCase):
    def test_first_completion_is_baseline_and_new_completion_chimes_once(self):
        completions = iter(["thread:old", "thread:new", "thread:new"])
        transport = FakeTransport()
        agent = CodexStatusAgent(
            collector=FakeCollector(),
            transport=transport,
            completion_reader=lambda: next(completions),
        )

        asyncio.run(agent.tick())
        asyncio.run(agent.tick())
        asyncio.run(agent.tick())

        status_messages = [item for item in transport.messages if item[0] == STATUS_MESSAGE]
        event_messages = [item for item in transport.messages if item[0] == EVENT_MESSAGE]
        self.assertEqual(len(status_messages), 3)
        self.assertEqual(
            event_messages,
            [(EVENT_MESSAGE, {"type": "chime", "agent": "codex"})],
        )

    def test_provided_status_is_sent_without_collecting_again(self):
        collector = FakeCollector()
        transport = FakeTransport()
        agent = CodexStatusAgent(
            collector=collector,
            transport=transport,
            completion_reader=lambda: None,
        )
        status = {"codex_model": "gpt-shared", "codex_today_min": 12}

        result = asyncio.run(agent.tick(status=status))

        self.assertEqual(result, status)
        self.assertEqual(collector.calls, 0)
        self.assertEqual(transport.messages[0], (STATUS_MESSAGE, status))


if __name__ == "__main__":
    unittest.main()
