"""Persistent serialized Codex App Server JSON-RPC client."""

from __future__ import annotations

import asyncio
import json
import select
import subprocess
import time
from typing import Any, Callable, Dict

from task_alerts import _codex_binary


MAX_RPC_LINE_BYTES = 256 * 1024


class AppServerError(RuntimeError):
    """A redacted App Server failure."""


class CodexAppServerClient:
    def __init__(
        self,
        *,
        process_factory: Callable[[], object] = None,
        timeout: float = 2.0,
    ) -> None:
        self._process_factory = process_factory or self._spawn
        self._timeout = timeout
        self._process = None
        self._next_id = 1
        self._initialized = False
        self._lock = None

    def _serialized_lock(self):
        if self._lock is None:
            self._lock = asyncio.Lock()
        return self._lock

    @staticmethod
    def _spawn():
        binary = _codex_binary()
        if binary is None:
            raise AppServerError("Codex App Server is unavailable")
        try:
            return subprocess.Popen(
                [binary, "app-server", "--stdio"],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                encoding="utf-8",
                bufsize=1,
            )
        except OSError as error:
            raise AppServerError("Codex App Server is unavailable") from error

    async def start_turn(self, thread_id: str, text: str) -> Dict[str, Any]:
        return await self._call_turn(
            "turn/start",
            {"threadId": thread_id, "input": self._input(text)},
        )

    async def steer_turn(
        self, thread_id: str, expected_turn_id: str, text: str
    ) -> Dict[str, Any]:
        return await self._call_turn(
            "turn/steer",
            {
                "threadId": thread_id,
                "expectedTurnId": expected_turn_id,
                "input": self._input(text),
            },
        )

    @staticmethod
    def _input(text: str):
        if not isinstance(text, str) or not text:
            raise ValueError("text must be a nonempty string")
        return [{"type": "text", "text": text}]

    async def _call_turn(self, method: str, params: Dict[str, Any]):
        async with self._serialized_lock():
            try:
                return await asyncio.to_thread(
                    self._call_turn_blocking, method, params
                )
            except AppServerError:
                raise
            except Exception as error:
                raise AppServerError("Codex App Server request failed") from error

    def _call_turn_blocking(self, method, params):
        if self._process is None:
            self._process = self._process_factory()
        if not self._initialized:
            self._request(
                "initialize",
                {
                    "clientInfo": {
                        "name": "stopwatch-codex-agent",
                        "title": "StopWatch Codex Agent",
                        "version": "2.0",
                    },
                    "capabilities": {},
                },
            )
            self._initialized = True
        return self._request(method, params)

    def _request(self, method, params):
        process = self._process
        if process is None or process.stdin is None or process.stdout is None:
            raise AppServerError("Codex App Server is unavailable")
        request_id = self._next_id
        self._next_id += 1
        try:
            process.stdin.write(
                json.dumps(
                    {
                        "jsonrpc": "2.0",
                        "id": request_id,
                        "method": method,
                        "params": params,
                    },
                    ensure_ascii=False,
                    separators=(",", ":"),
                )
                + "\n"
            )
            process.stdin.flush()
        except (OSError, ValueError) as error:
            raise AppServerError("Codex App Server request failed") from error
        deadline = time.monotonic() + self._timeout
        while time.monotonic() < deadline:
            try:
                fileno = process.stdout.fileno()
            except (AttributeError, OSError):
                line = process.stdout.readline(MAX_RPC_LINE_BYTES + 1)
                if not line:
                    time.sleep(min(0.001, max(0, deadline - time.monotonic())))
                    continue
            else:
                readable, _, _ = select.select(
                    [fileno], [], [], max(0, deadline - time.monotonic())
                )
                if not readable:
                    break
                line = process.stdout.readline(MAX_RPC_LINE_BYTES + 1)
            if not line:
                continue
            if len(line.encode("utf-8")) > MAX_RPC_LINE_BYTES:
                raise AppServerError(
                    "Codex App Server returned an oversized response"
                )
            try:
                message = json.loads(line)
            except json.JSONDecodeError:
                continue
            if not isinstance(message, dict) or message.get("id") != request_id:
                continue
            if "error" in message:
                raise AppServerError("Codex App Server rejected the request")
            result = message.get("result")
            if not isinstance(result, dict):
                raise AppServerError("Codex App Server returned an invalid response")
            return result
        raise AppServerError("Codex App Server request timed out")

    async def close(self, timeout: float = 1.0) -> None:
        async with self._serialized_lock():
            process = self._process
            self._process = None
            self._initialized = False
            if process is None:
                return
            try:
                process.terminate()
                await asyncio.wait_for(
                    asyncio.to_thread(process.wait, timeout), timeout=timeout
                )
            except Exception:
                return
