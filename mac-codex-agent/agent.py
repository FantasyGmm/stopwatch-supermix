#!/usr/bin/env python3
"""Continuously deliver local Codex status to a StopWatch over BLE."""

import asyncio
import dataclasses
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
GATT_OPERATION_TIMEOUT_SECONDS = 5.0


@dataclasses.dataclass
class _VoiceIngressGate:
    lock: threading.Lock = dataclasses.field(default_factory=threading.Lock)
    pending: int = 0
    overflow: bool = False
    enabled: bool = True


@dataclasses.dataclass(frozen=True)
class _VoiceGeneration:
    queue: asyncio.Queue
    failure: asyncio.Future
    consumer: asyncio.Task
    ingress: _VoiceIngressGate


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
        self._voice_generation = None
        self._voice_notify_started = False
        self._voice_disabled = False

    @property
    def connected(self) -> bool:
        return bool(self._client and self._client.is_connected)

    @property
    def voice_failure(self):
        generation = self._voice_generation
        return generation.failure if generation is not None else None

    @property
    def _voice_queue(self):
        generation = self._voice_generation
        return generation.queue if generation is not None else None

    @property
    def _voice_consumer(self):
        generation = self._voice_generation
        return generation.consumer if generation is not None else None

    @property
    def _voice_failure(self):
        return self.voice_failure

    @property
    def _voice_ingress_pending(self):
        generation = self._voice_generation
        if generation is None:
            return 0
        with generation.ingress.lock:
            return generation.ingress.pending

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
        if self._voice_generation is not None:
            raise RuntimeError("voice notification generation is already active")
        if self._voice_notify_started:
            raise RuntimeError("voice notification cleanup is incomplete")
        loop = asyncio.get_running_loop()
        self._voice_disabled = False
        queue = asyncio.Queue(maxsize=VOICE_NOTIFICATION_QUEUE_SIZE)
        failure = loop.create_future()
        consumer = asyncio.create_task(self._consume_voice(queue, handler))
        generation = _VoiceGeneration(
            queue, failure, consumer, _VoiceIngressGate()
        )
        self._voice_generation = generation
        consumer.add_done_callback(
            lambda task: self._voice_consumer_done(generation, task)
        )

        def on_notification(_sender, data) -> None:
            payload = bytes(data)
            callback = None
            payload_admitted = False
            with generation.ingress.lock:
                if (
                    not generation.ingress.enabled
                    or generation.ingress.overflow
                ):
                    return
                if generation.ingress.pending >= VOICE_NOTIFICATION_QUEUE_SIZE:
                    generation.ingress.overflow = True
                    callback = self._signal_voice_overflow
                else:
                    generation.ingress.pending += 1
                    callback = self._deliver_voice_payload
                    payload_admitted = True
            try:
                if payload_admitted:
                    loop.call_soon_threadsafe(callback, generation, payload)
                else:
                    loop.call_soon_threadsafe(callback, generation)
            except RuntimeError:
                if payload_admitted:
                    self._release_voice_ingress(generation)

        self._voice_notify_started = True
        try:
            await asyncio.wait_for(
                self._client.start_notify(VOICE_TX_UUID, on_notification),
                timeout=GATT_OPERATION_TIMEOUT_SECONDS,
            )
            await asyncio.wait_for(
                self._client.write_gatt_char(
                    VOICE_CONTROL_UUID, ready_payload, response=True
                ),
                timeout=GATT_OPERATION_TIMEOUT_SECONDS,
            )
        except BaseException:
            consumer = self._retire_voice_generation(generation)
            try:
                if self.connected and self._voice_notify_started:
                    try:
                        await asyncio.wait_for(
                            self._client.stop_notify(VOICE_TX_UUID),
                            timeout=GATT_OPERATION_TIMEOUT_SECONDS,
                        )
                    except Exception:
                        pass
                    else:
                        self._voice_notify_started = False
            finally:
                if consumer is not None:
                    try:
                        await consumer
                    except asyncio.CancelledError:
                        pass
                    except Exception:
                        pass
            raise

    async def disable_voice(self, unavailable_payload: Optional[bytes]) -> None:
        if self._voice_disabled:
            return
        consumer = self.retire_voice_generation()
        failure = None
        stop_failed = False
        try:
            if self.connected and unavailable_payload is not None:
                try:
                    await asyncio.wait_for(
                        self._client.write_gatt_char(
                            VOICE_CONTROL_UUID, unavailable_payload, response=True
                        ),
                        timeout=GATT_OPERATION_TIMEOUT_SECONDS,
                    )
                except Exception as error:
                    failure = error
            if self.connected and self._voice_notify_started:
                try:
                    await asyncio.wait_for(
                        self._client.stop_notify(VOICE_TX_UUID),
                        timeout=GATT_OPERATION_TIMEOUT_SECONDS,
                    )
                except Exception as error:
                    stop_failed = True
                    if failure is None:
                        failure = error
                else:
                    self._voice_notify_started = False
            if not stop_failed:
                self._voice_disabled = True
        finally:
            if consumer is not None:
                try:
                    await consumer
                except asyncio.CancelledError:
                    pass
                except Exception:
                    pass
        if failure is not None:
            raise failure

    def retire_voice_generation(self):
        return self._retire_voice_generation()

    def _retire_voice_generation(self, expected=None):
        generation = self._voice_generation
        if generation is None or (
            expected is not None and generation is not expected
        ):
            return None
        with generation.ingress.lock:
            generation.ingress.enabled = False
        if self._voice_generation is generation:
            self._voice_generation = None
        if not generation.consumer.done():
            generation.consumer.cancel()
        return generation.consumer

    def _enqueue_voice(
        self, generation: _VoiceGeneration, payload: bytes
    ) -> None:
        if self._voice_generation is not generation:
            return
        try:
            generation.queue.put_nowait(payload)
        except asyncio.QueueFull:
            if not generation.failure.done():
                generation.failure.set_result(
                    RuntimeError("voice notification queue overflow")
                )
            generation.consumer.cancel()

    def _deliver_voice_payload(
        self, generation: _VoiceGeneration, payload: bytes
    ) -> None:
        try:
            with generation.ingress.lock:
                enabled = generation.ingress.enabled
            if enabled and self._voice_generation is generation:
                self._enqueue_voice(generation, payload)
        finally:
            self._release_voice_ingress(generation)

    @staticmethod
    def _release_voice_ingress(generation: _VoiceGeneration) -> None:
        with generation.ingress.lock:
            if generation.ingress.pending:
                generation.ingress.pending -= 1

    def _signal_voice_overflow(self, generation: _VoiceGeneration) -> None:
        with generation.ingress.lock:
            enabled = generation.ingress.enabled
        if not enabled or self._voice_generation is not generation:
            return
        if not generation.failure.done():
            generation.failure.set_result(
                RuntimeError("voice notification queue overflow")
            )
        generation.consumer.cancel()

    @staticmethod
    async def _consume_voice(queue: asyncio.Queue, handler) -> None:
        while True:
            payload = await queue.get()
            await handler(payload)

    @staticmethod
    def _voice_consumer_done(
        generation: _VoiceGeneration, task: asyncio.Task
    ) -> None:
        if task.cancelled():
            return
        error = task.exception()
        if error is not None and not generation.failure.done():
            generation.failure.set_result(error)

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
            await asyncio.wait_for(
                self._client.write_gatt_char(
                    STATUS_RX_UUID, frame, response=True
                ),
                timeout=GATT_OPERATION_TIMEOUT_SECONDS,
            )
            if (
                (index + 1) % FRAME_BURST_SIZE == 0
                and index + 1 < len(frames)
            ):
                await asyncio.sleep(FRAME_BURST_PAUSE_SECONDS)
