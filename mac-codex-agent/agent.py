#!/usr/bin/env python3
"""Continuously deliver local Codex status to a StopWatch over BLE."""

import asyncio
import datetime
import json
import threading
import time
from typing import Any, Callable, Optional

from codex_adapter import CodexStateCollector
from protocol import EVENT_MESSAGE, MAX_MESSAGE_SIZE, STATUS_MESSAGE, encode_message


DEVICE_NAME = "StopWatch-Codex"
SERVICE_UUID = "7a230001-5474-4f50-5741-544348434f44"
STATUS_RX_UUID = "7a230002-5474-4f50-5741-544348434f44"
VOICE_CONTROL_UUID = "7a230003-5474-4f50-5741-544348434f44"
VOICE_TX_UUID = "7a230004-5474-4f50-5741-544348434f44"
STATUS_INTERVAL_SECONDS = 2.0
SAFE_WRITE_SIZE = 20
FRAME_BURST_SIZE = 8
FRAME_BURST_PAUSE_SECONDS = 0.02
MAX_WATCH_ALERTS = 8
MAX_PROJECT_LENGTH = 32
MAX_TITLE_LENGTH = 48
VOICE_NOTIFICATION_QUEUE_SIZE = 32


def _unix_time_ms() -> int:
    return time.time_ns() // 1_000_000


def _utc_offset_minutes() -> int:
    offset = datetime.datetime.now().astimezone().utcoffset()
    return int(offset.total_seconds() // 60) if offset is not None else 0


class CodexStatusAgent:
    def __init__(
        self,
        collector: CodexStateCollector,
        transport: Any,
        time_ms: Callable[[], int] = _unix_time_ms,
        utc_offset_minutes: Callable[[], int] = _utc_offset_minutes,
    ):
        self.collector = collector
        self.transport = transport
        self._time_ms = time_ms
        self._utc_offset_minutes = utc_offset_minutes

    async def sync_time(self) -> None:
        payload = {
            "type": "time_sync",
            "unix_ms": self._time_ms(),
            "utc_offset_minutes": self._utc_offset_minutes(),
        }
        await self.transport.send_json(
            json.dumps(payload, separators=(",", ":")), EVENT_MESSAGE
        )

    async def tick(self, status=None):
        if status is None:
            status = await asyncio.to_thread(self.collector.get_state)
        watch_status = _watch_status(status)
        await self.transport.send_json(
            json.dumps(watch_status, separators=(",", ":")), STATUS_MESSAGE
        )

        alert_ids = await asyncio.to_thread(
            self.collector.pending_watch_notification_ids
        )
        if alert_ids:
            await self.transport.send_json(
                json.dumps(
                    {"type": "chime", "agent": "codex"},
                    separators=(",", ":"),
                ),
                EVENT_MESSAGE,
            )
            await asyncio.to_thread(
                self.collector.mark_watch_notified, alert_ids
            )
        return status


def _watch_status(status):
    result = dict(status)
    alerts = status.get("codex_alerts")
    if isinstance(alerts, list):
        result["codex_alerts"] = [
            {
                "project": str(item.get("project") or "")[:MAX_PROJECT_LENGTH],
                "title": str(item.get("title") or "")[:MAX_TITLE_LENGTH],
                "time": str(item.get("time") or ""),
                "state": str(item.get("state") or ""),
                "outcome": (
                    str(item.get("outcome"))
                    if item.get("outcome") in ("completed", "waiting", "running")
                    else "completed"
                ),
            }
            for item in alerts[:MAX_WATCH_ALERTS]
            if isinstance(item, dict)
        ]
        while result["codex_alerts"] and len(
            json.dumps(result, separators=(",", ":")).encode("utf-8")
        ) > MAX_MESSAGE_SIZE:
            result["codex_alerts"].pop()
    return result


class BleStatusTransport:
    def __init__(self, device_name: str = DEVICE_NAME):
        self.device_name = device_name
        self._client = None
        self._sequence = 0
        self._disconnected = asyncio.Event()
        self._voice_queue = None
        self._voice_consumer = None
        self._voice_failure = None
        self._voice_notify_started = False
        self._voice_disabled = False
        self._voice_ingress_lock = threading.Lock()
        self._voice_ingress_pending = 0
        self._voice_ingress_overflow = False
        self._voice_ingress_enabled = False

    @property
    def connected(self) -> bool:
        return bool(self._client and self._client.is_connected)

    @property
    def voice_failure(self):
        return self._voice_failure

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
        self._voice_disabled = False

        def on_disconnect(_client) -> None:
            self._disconnected.set()

        client = BleakClient(device, disconnected_callback=on_disconnect)
        await client.connect()
        self._client = client
        return client.is_connected

    async def enable_voice(self, ready_payload: bytes, handler) -> None:
        if not self.connected:
            raise ConnectionError("StopWatch BLE connection is not active")
        loop = asyncio.get_running_loop()
        self._voice_disabled = False
        self._voice_queue = asyncio.Queue(maxsize=VOICE_NOTIFICATION_QUEUE_SIZE)
        self._voice_failure = loop.create_future()
        self._voice_consumer = asyncio.create_task(self._consume_voice(handler))
        self._voice_consumer.add_done_callback(self._voice_consumer_done)
        with self._voice_ingress_lock:
            self._voice_ingress_pending = 0
            self._voice_ingress_overflow = False
            self._voice_ingress_enabled = True

        def on_notification(_sender, data) -> None:
            payload = bytes(data)
            callback = None
            payload_admitted = False
            with self._voice_ingress_lock:
                if (
                    not self._voice_ingress_enabled
                    or self._voice_ingress_overflow
                ):
                    return
                if self._voice_ingress_pending >= VOICE_NOTIFICATION_QUEUE_SIZE:
                    self._voice_ingress_overflow = True
                    callback = self._signal_voice_overflow
                else:
                    self._voice_ingress_pending += 1
                    callback = self._deliver_voice_payload
                    payload_admitted = True
            try:
                if payload_admitted:
                    loop.call_soon_threadsafe(callback, payload)
                else:
                    loop.call_soon_threadsafe(callback)
            except RuntimeError:
                if payload_admitted:
                    self._release_voice_ingress()

        self._voice_notify_started = True
        await self._client.start_notify(VOICE_TX_UUID, on_notification)
        await self._client.write_gatt_char(
            VOICE_CONTROL_UUID, ready_payload, response=True
        )

    async def disable_voice(self, unavailable_payload: Optional[bytes]) -> None:
        if self._voice_disabled:
            return
        with self._voice_ingress_lock:
            self._voice_ingress_enabled = False
        failure = None
        stop_failed = False
        try:
            if self.connected and unavailable_payload is not None:
                try:
                    await self._client.write_gatt_char(
                        VOICE_CONTROL_UUID, unavailable_payload, response=True
                    )
                except Exception as error:
                    failure = error
            if self.connected and self._voice_notify_started:
                try:
                    await self._client.stop_notify(VOICE_TX_UUID)
                except Exception as error:
                    stop_failed = True
                    if failure is None:
                        failure = error
                else:
                    self._voice_notify_started = False
            if not stop_failed:
                self._voice_disabled = True
        finally:
            consumer = self._voice_consumer
            self._voice_consumer = None
            self._voice_queue = None
            self._voice_failure = None
            if consumer is not None and not consumer.done():
                consumer.cancel()
            if consumer is not None:
                try:
                    await consumer
                except asyncio.CancelledError:
                    pass
                except Exception:
                    pass
        if failure is not None:
            raise failure

    def _enqueue_voice(self, payload: bytes) -> None:
        queue = self._voice_queue
        failure = self._voice_failure
        if queue is None or failure is None or failure.done():
            return
        try:
            queue.put_nowait(payload)
        except asyncio.QueueFull:
            failure.set_result(RuntimeError("voice notification queue overflow"))
            if self._voice_consumer is not None:
                self._voice_consumer.cancel()

    def _deliver_voice_payload(self, payload: bytes) -> None:
        try:
            with self._voice_ingress_lock:
                enabled = self._voice_ingress_enabled
            if enabled:
                self._enqueue_voice(payload)
        finally:
            self._release_voice_ingress()

    def _release_voice_ingress(self) -> None:
        with self._voice_ingress_lock:
            if self._voice_ingress_pending:
                self._voice_ingress_pending -= 1

    def _signal_voice_overflow(self) -> None:
        with self._voice_ingress_lock:
            enabled = self._voice_ingress_enabled
        if not enabled:
            return
        failure = self._voice_failure
        if failure is not None and not failure.done():
            failure.set_result(RuntimeError("voice notification queue overflow"))
        if self._voice_consumer is not None:
            self._voice_consumer.cancel()

    async def _consume_voice(self, handler) -> None:
        while True:
            payload = await self._voice_queue.get()
            await handler(payload)

    def _voice_consumer_done(self, task: asyncio.Task) -> None:
        if task.cancelled():
            return
        error = task.exception()
        if error is not None and self._voice_failure is not None:
            if not self._voice_failure.done():
                self._voice_failure.set_result(error)

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
        for index, frame in enumerate(frames):
            await self._client.write_gatt_char(
                STATUS_RX_UUID, frame, response=True
            )
            if (
                (index + 1) % FRAME_BURST_SIZE == 0
                and index + 1 < len(frames)
            ):
                await asyncio.sleep(FRAME_BURST_PAUSE_SECONDS)
