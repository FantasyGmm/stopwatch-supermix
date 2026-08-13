import asyncio
import io
import json
import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from codex_app_server import AppServerError, CodexAppServerClient


class FakeProcess:
    def __init__(self, responses):
        self.stdin = io.StringIO()
        self.stdout = io.StringIO("".join(json.dumps(item) + "\n" for item in responses))
        self.returncode = None
        self.terminated = False

    def poll(self):
        return self.returncode

    def terminate(self):
        self.terminated = True
        self.returncode = 0

    def wait(self, timeout=None):
        return self.returncode


class CodexAppServerClientTests(unittest.IsolatedAsyncioTestCase):
    async def test_initializes_once_then_starts_and_skips_notifications(self):
        process = FakeProcess(
            [
                {"method": "server/notice", "params": {}},
                {"id": 1, "result": {"serverInfo": {}}},
                {"method": "turn/started", "params": {}},
                {"id": 2, "result": {"turn": {"id": "turn-1"}}},
                {"id": 3, "result": {"turn": {"id": "turn-2"}}},
            ]
        )
        client = CodexAppServerClient(process_factory=lambda: process, timeout=0.1)
        await client.start_turn("thread-1", "hello")
        await client.start_turn("thread-1", "again")
        requests = [json.loads(line) for line in process.stdin.getvalue().splitlines()]
        self.assertEqual([item["method"] for item in requests], ["initialize", "turn/start", "turn/start"])
        self.assertEqual(
            requests[1]["params"],
            {"threadId": "thread-1", "input": [{"type": "text", "text": "hello"}]},
        )

    async def test_steer_uses_exact_expected_turn_and_errors_redact_text(self):
        secret = "private transcript"
        process = FakeProcess(
            [
                {"id": 1, "result": {}},
                {"id": 2, "error": {"code": -32000, "message": secret}},
            ]
        )
        client = CodexAppServerClient(process_factory=lambda: process, timeout=0.1)
        with self.assertRaises(AppServerError) as raised:
            await client.steer_turn("thread-2", "turn-exact", secret)
        request = json.loads(process.stdin.getvalue().splitlines()[1])
        self.assertEqual(request["params"]["expectedTurnId"], "turn-exact")
        self.assertNotIn(secret, str(raised.exception))

    async def test_timeout_does_not_expose_text(self):
        secret = "never expose me"
        process = FakeProcess([])
        client = CodexAppServerClient(process_factory=lambda: process, timeout=0.01)
        with self.assertRaises(AppServerError) as raised:
            await client.start_turn("thread", secret)
        self.assertNotIn(secret, str(raised.exception))


if __name__ == "__main__":
    unittest.main()
