#!/usr/bin/env python3
"""Continuously deliver local Codex status to a StopWatch over BLE."""

import asyncio
import json
from typing import Any, Callable, Optional

from codex_adapter import CodexStateCollector
from completion import read_latest_completion_signal
from protocol import EVENT_MESSAGE, STATUS_MESSAGE, encode_message


DEVICE_NAME = "StopWatch-Codex"
SERVICE_UUID = "7a230001-5474-4f50-5741-544348434f44"
STATUS_RX_UUID = "7a230002-5474-4f50-5741-544348434f44"
STATUS_INTERVAL_SECONDS = 2.0
SAFE_WRITE_SIZE = 20


class CodexStatusAgent:
    def __init__(
        self,
        collector: CodexStateCollector,
        transport: Any,
        completion_reader: Callable[[], Optional[str]] = read_latest_completion_signal,
    ):
        self.collector = collector
        self.transport = transport
        self.completion_reader = completion_reader
        self._last_completion: Optional[str] = None
        self._completion_initialized = False

    async def tick(self, status=None):
        if status is None:
            status = await asyncio.to_thread(self.collector.get_state)
        await self.transport.send_json(
            json.dumps(status, separators=(",", ":")), STATUS_MESSAGE
        )

        completion = await asyncio.to_thread(self.completion_reader)
        if not self._completion_initialized:
            self._last_completion = completion
            self._completion_initialized = True
        elif completion and completion != self._last_completion:
            await self.transport.send_json(
                json.dumps(
                    {"type": "chime", "agent": "codex"},
                    separators=(",", ":"),
                ),
                EVENT_MESSAGE,
            )
            self._last_completion = completion
        return status


class BleStatusTransport:
    def __init__(self, device_name: str = DEVICE_NAME):
        self.device_name = device_name
        self._client = None
        self._sequence = 0
        self._disconnected = asyncio.Event()

    @property
    def connected(self) -> bool:
        return bool(self._client and self._client.is_connected)

    async def connect(self, on_device_found: Optional[Callable[[], None]] = None) -> bool:
        try:
            from bleak import BleakClient, BleakScanner
        except ImportError as error:
            raise RuntimeError(
                "bleak is not installed; run pip install -r requirements.txt"
            ) from error

        def matches(device, advertisement) -> bool:
            names = {device.name, advertisement.local_name}
            services = {item.lower() for item in advertisement.service_uuids or []}
            return self.device_name in names or SERVICE_UUID in services

        device = await BleakScanner.find_device_by_filter(matches, timeout=8.0)
        if device is None:
            return False
        if on_device_found:
            on_device_found()

        self._disconnected.clear()

        def on_disconnect(_client) -> None:
            self._disconnected.set()

        client = BleakClient(device, disconnected_callback=on_disconnect)
        await client.connect()
        self._client = client
        return client.is_connected

    async def disconnect(self) -> None:
        client = self._client
        self._client = None
        if client and client.is_connected:
            await client.disconnect()

    async def send_json(self, payload: str, message_type: int) -> None:
        if not self.connected:
            raise ConnectionError("StopWatch BLE connection is not active")
        sequence = self._sequence
        self._sequence = (self._sequence + 1) & 0xFFFF
        frames = encode_message(
            payload.encode("utf-8"),
            message_type=message_type,
            sequence=sequence,
            max_write_size=SAFE_WRITE_SIZE,
        )
        for frame in frames:
            await self._client.write_gatt_char(
                STATUS_RX_UUID, frame, response=True
            )
