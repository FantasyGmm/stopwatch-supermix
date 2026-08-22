#!/usr/bin/env python3
"""Continuously deliver local Codex status to a StopWatch over BLE."""

import asyncio
import dataclasses
import datetime
import hashlib
import json
import logging
import math
import re
import secrets
import threading
import time
from typing import Any, Callable, Mapping, Optional

from pairing_client import (
    AuthenticationRequest,
    BeginPairingRequest,
)
from protocol import EVENT_MESSAGE, MAX_MESSAGE_SIZE, STATUS_MESSAGE, encode_message
from secure_pairing_wire import (
    WireMessageType,
    decode_authentication_challenge,
    decode_credential_envelope,
    decode_discovery_response,
    decode_frame as decode_pairing_frame,
    decode_pairing_offer,
    encode_authentication_proof,
    encode_authentication_request,
    encode_credential_ack,
    encode_discovery_request,
    encode_pairing_confirmation,
    encode_pairing_request,
)
from task_detail_crypto import (
    MAX_TASK_REVISION,
    encrypt_task_detail,
)
from task_routes import TaskRouteRegistry


LOGGER = logging.getLogger(__name__)
DEVICE_NAME = "StopWatch-Codex"
SERVICE_UUID = "7a230001-5474-4f50-5741-544348434f44"
STATUS_RX_UUID = "7a230002-5474-4f50-5741-544348434f44"
VOICE_CONTROL_UUID = "7a230003-5474-4f50-5741-544348434f44"
VOICE_TX_UUID = "7a230004-5474-4f50-5741-544348434f44"
PAIRING_SERVICE_UUID = "7a230006-5474-4f50-5741-544348434f44"
PAIRING_CONTROL_UUID = "7a230007-5474-4f50-5741-544348434f44"
PAIRING_TX_UUID = "7a230008-5474-4f50-5741-544348434f44"
INPUT_CONTROL_UUID = "7a230009-5474-4f50-5741-544348434f44"
INPUT_TX_UUID = "7a23000a-5474-4f50-5741-544348434f44"
STATUS_INTERVAL_SECONDS = 2.0
SAFE_WRITE_SIZE = 20
MAX_STATUS_WRITE_SIZE = 256
FRAME_BURST_SIZE = 8
FRAME_BURST_PAUSE_SECONDS = 0.02
MAX_WATCH_ALERTS = 8
MAX_CODEX_MODEL_LENGTH = 64
MAX_CODEX_EFFORT_LENGTH = 32
MAX_PROJECT_LENGTH = 32
MAX_TITLE_LENGTH = 48
MAX_TASK_DETAIL_BYTES = 12 * 1024
MAX_TASK_DETAIL_CHUNK_BYTES = 1300
TASK_DETAIL_TRUNCATION_MARKER = "\n\n[内容过长，手表仅显示前 12 KiB]"
MAX_DETAIL_LENGTH = 72
MAX_TRAE_TASKS = 4
MAX_TRAE_TASK_COUNT = 65535
MAX_TRAE_REVISION = 2147483647
MAX_TRAE_MESSAGE_SIZE = 1024
TRAE_HEARTBEAT_SECONDS = 6.0
VOICE_AUDIO_FRAME_MS = 20
VOICE_STARTUP_BUFFER_MS = 10_000
VOICE_NOTIFICATION_QUEUE_SIZE = (
    VOICE_STARTUP_BUFFER_MS // VOICE_AUDIO_FRAME_MS + 2
)
GATT_OPERATION_TIMEOUT_SECONDS = 5.0
PAIRING_CONFIRM_TIMEOUT_SECONDS = 65.0
PAIRING_RESPONSE_QUEUE_SIZE = 8
INPUT_NOTIFICATION_QUEUE_SIZE = 8
_TASK_ROUTE_PATTERN = re.compile(r"^[0-9a-f]{16}$")
_CODEX_NUMERIC_LIMITS = {
    "codex_5h_left_pct": 100,
    "codex_week_left_pct": 100,
    "codex_alert_pending": 65535,
    "codex_alert_running": 65535,
    "codex_alert_completed": 65535,
}


@dataclasses.dataclass(frozen=True)
class WatchDiscovery:
    watch_id: bytes
    key_epoch: int
    watch_identity_public: bytes


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


@dataclasses.dataclass
class _InputIngressGate:
    lock: threading.Lock = dataclasses.field(default_factory=threading.Lock)
    pending: int = 0
    enabled: bool = True


@dataclasses.dataclass(frozen=True)
class _InputGeneration:
    queue: asyncio.Queue
    consumer: asyncio.Task
    ingress: _InputIngressGate


async def _await_cancelled_task(task: asyncio.Task) -> None:
    try:
        await task
    except asyncio.CancelledError:
        pass
    except Exception:
        pass


def _unix_time_ms() -> int:
    return time.time_ns() // 1_000_000


def _utc_offset_minutes() -> int:
    offset = datetime.datetime.now().astimezone().utcoffset()
    return int(offset.total_seconds() // 60) if offset is not None else 0


def _new_task_detail_nonce() -> bytes:
    nonce = bytes(8)
    while not any(nonce):
        nonce = secrets.token_bytes(8)
    return nonce


class CodexStatusAgent:
    def __init__(
        self,
        collector: Any,
        transport: Any,
        time_ms: Callable[[], int] = _unix_time_ms,
        utc_offset_minutes: Callable[[], int] = _utc_offset_minutes,
        monotonic: Callable[[], float] = time.monotonic,
        detail_key=None,
        detail_key_epoch=None,
        detail_nonce_factory: Callable[[], bytes] = _new_task_detail_nonce,
        task_route_registry=None,
    ):
        self.collector = collector
        self.transport = transport
        self._time_ms = time_ms
        self._utc_offset_minutes = utc_offset_minutes
        self._monotonic = monotonic
        self._last_trae_fingerprint = None
        self._last_trae_sent_at = None
        self._delivered_alert_ids = set()
        self._acknowledged_alert_ids = set()
        self._task_detail_cache = {}
        self._task_detail_sequence = 0
        self._task_detail_key = None
        self._task_detail_key_epoch = None
        self._task_detail_nonce = None
        self._task_detail_nonce_factory = detail_nonce_factory
        self._task_detail_used_nonces = set()
        # Collision tombstones intentionally live for one connected session.
        # The runtime injects and resets its shared registry on reconnect.
        self._task_route_registry = (
            task_route_registry
            if task_route_registry is not None
            else TaskRouteRegistry()
        )
        self.update_detail_credential(detail_key, detail_key_epoch)

    def update_detail_credential(self, detail_key, detail_key_epoch) -> None:
        self._task_detail_cache = {}
        self._task_detail_sequence = 0
        self._task_detail_key = None
        self._task_detail_key_epoch = None
        self._task_detail_nonce = None
        if (
            isinstance(detail_key, bytes)
            and len(detail_key) == 16
            and isinstance(detail_key_epoch, int)
            and not isinstance(detail_key_epoch, bool)
            and 1 <= detail_key_epoch <= 0xFFFFFFFF
        ):
            for _attempt in range(16):
                try:
                    nonce = self._task_detail_nonce_factory()
                except Exception:
                    break
                if (
                    isinstance(nonce, bytes)
                    and len(nonce) == 8
                    and any(nonce)
                    and nonce not in self._task_detail_used_nonces
                ):
                    self._task_detail_used_nonces.add(nonce)
                    self._task_detail_key = detail_key
                    self._task_detail_key_epoch = detail_key_epoch
                    self._task_detail_nonce = nonce
                    break

    def disable_detail_credential(self) -> None:
        """Fail closed without affecting compact plaintext status updates."""
        self.update_detail_credential(None, None)

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
        raw_alert_ids = status.get("_codex_watch_notification_ids")
        alert_ids = (
            tuple(raw_alert_ids)
            if isinstance(raw_alert_ids, (list, tuple))
            else ()
        )
        active_alerts = _normalized_active_alerts(
            status, route_registry=self._task_route_registry
        )
        watch_status = _watch_codex_status(status, active_alerts=active_alerts)
        await self.transport.send_json(
            json.dumps(
                watch_status,
                ensure_ascii=False,
                separators=(",", ":"),
            ),
            STATUS_MESSAGE,
        )

        retained_routes = {
            item["route"]
            for item in watch_status.get("codex_alerts", [])
        }
        details = (
            task_detail_messages(
                status,
                retained_routes=retained_routes,
                active_alerts=active_alerts,
            )
            if self._task_detail_key is not None
            else []
        )
        details_by_route = {}
        for detail in details:
            details_by_route.setdefault(detail["route"], []).append(detail)
        self._task_detail_cache = {
            route: fingerprint
            for route, fingerprint in self._task_detail_cache.items()
            if route in details_by_route
        }
        for route, messages in details_by_route.items():
            revision = messages[0]["revision"]
            bounded_text = "".join(message["text"] for message in messages)
            fingerprint = (
                route,
                revision,
                hashlib.sha256(bounded_text.encode("utf-8")).hexdigest(),
            )
            if self._task_detail_cache.get(route) == fingerprint:
                continue
            for message in messages:
                sequence = self._next_task_detail_sequence()
                envelope = encrypt_task_detail(
                    self._task_detail_key,
                    key_epoch=self._task_detail_key_epoch,
                    exchange_nonce=self._task_detail_nonce,
                    sequence=sequence,
                    route=route,
                    revision=message["revision"],
                    index=message["index"],
                    count=message["count"],
                    text=message["text"],
                )
                if len(_compact_json_bytes(envelope)) > MAX_MESSAGE_SIZE:
                    raise ValueError("encrypted task detail exceeds BLE limit")
                await self.transport.send_json(
                    json.dumps(
                        envelope,
                        ensure_ascii=False,
                        separators=(",", ":"),
                    ),
                    STATUS_MESSAGE,
                )
            self._task_detail_cache[route] = fingerprint

        if any(str(key).startswith("trae_") for key in status):
            trae_status = _watch_trae_status(status)
            fingerprint = _trae_fingerprint(trae_status)
            now = self._monotonic()
            if (
                fingerprint != self._last_trae_fingerprint
                or self._last_trae_sent_at is None
                or now - self._last_trae_sent_at >= TRAE_HEARTBEAT_SECONDS
            ):
                await self.transport.send_json(
                    json.dumps(
                        trae_status,
                        ensure_ascii=False,
                        separators=(",", ":"),
                    ),
                    STATUS_MESSAGE,
                )
                self._last_trae_fingerprint = fingerprint
                self._last_trae_sent_at = now

        new_alert_ids = tuple(
            alert_id
            for alert_id in alert_ids
            if alert_id not in self._delivered_alert_ids
        )
        if new_alert_ids:
            await self.transport.send_json(
                json.dumps(
                    {"type": "chime", "agent": "codex"},
                    separators=(",", ":"),
                ),
                EVENT_MESSAGE,
            )
            self._delivered_alert_ids.update(new_alert_ids)
        unacknowledged_ids = tuple(
            alert_id
            for alert_id in alert_ids
            if alert_id in self._delivered_alert_ids
            and alert_id not in self._acknowledged_alert_ids
        )
        if unacknowledged_ids:
            try:
                await asyncio.to_thread(
                    self.collector.mark_watch_notified,
                    unacknowledged_ids,
                )
            except asyncio.CancelledError:
                raise
            except Exception as error:
                LOGGER.warning(
                    "Watch notification acknowledgement deferred: %s",
                    type(error).__name__,
                )
            else:
                self._acknowledged_alert_ids.update(unacknowledged_ids)
        return status

    def _next_task_detail_sequence(self) -> int:
        if self._task_detail_sequence >= 0xFFFFFFFF:
            raise OverflowError("task detail sequence exhausted")
        self._task_detail_sequence += 1
        return self._task_detail_sequence


def _normalized_active_alerts(status, route_registry=None):
    if not isinstance(status, Mapping):
        return []
    alerts = status.get("codex_alerts")
    if not isinstance(alerts, list):
        return []
    observed_routes = [
        item["route"]
        for item in alerts
        if isinstance(item, dict)
        and isinstance(item.get("route"), str)
        and _TASK_ROUTE_PATTERN.fullmatch(item["route"]) is not None
    ]
    registry = route_registry or TaskRouteRegistry()
    registry.update([], observed_routes=observed_routes)
    return [
        item
        for item in alerts
        if isinstance(item, dict)
        and item.get("state") in ("pending", "running")
        and isinstance(item.get("route"), str)
        and _TASK_ROUTE_PATTERN.fullmatch(item["route"]) is not None
        and not registry.is_tombstoned(item["route"])
    ]


def _watch_codex_status(
    status: Mapping[str, object], active_alerts=None
):
    result = {}
    if "codex_model" in status:
        result["codex_model"] = _strict_bounded_text(
            status.get("codex_model"), MAX_CODEX_MODEL_LENGTH
        ) or ""
    if "codex_effort" in status:
        result["codex_effort"] = _strict_bounded_text(
            status.get("codex_effort"), MAX_CODEX_EFFORT_LENGTH
        ) or ""
    for key, maximum in _CODEX_NUMERIC_LIMITS.items():
        if key not in status:
            continue
        value = status.get(key)
        if value is None and key in (
            "codex_5h_left_pct",
            "codex_week_left_pct",
        ):
            result[key] = None
        else:
            result[key] = min(_nonnegative_integer(value), maximum)
    if "codex_alert_revision" in status:
        result["codex_alert_revision"] = _task_revision(
            status.get("codex_alert_revision")
        )
    alerts = status.get("codex_alerts")
    if isinstance(alerts, list):
        normalized_alerts = (
            _normalized_active_alerts(status)
            if active_alerts is None
            else active_alerts
        )
        display_alerts = []
        for item in normalized_alerts:
            project = _strict_bounded_text(
                item.get("project"), MAX_PROJECT_LENGTH
            )
            title = _strict_bounded_text(item.get("title"), MAX_TITLE_LENGTH)
            if project is None or title is None:
                continue
            display_alerts.append(
                {
                    "route": item["route"],
                    "project": project,
                    "title": title,
                    "state": item["state"],
                }
            )
        display_alerts.sort(
            key=lambda item: 0 if item.get("state") == "pending" else 1
        )
        result["codex_alerts"] = [
            item
            for item in display_alerts[:MAX_WATCH_ALERTS]
        ]
        while result["codex_alerts"] and len(
            _compact_json_bytes(result)
        ) > MAX_MESSAGE_SIZE:
            result["codex_alerts"].pop()
    if len(_compact_json_bytes(result)) > MAX_MESSAGE_SIZE:
        result.pop("codex_effort", None)
        result.pop("codex_model", None)
    if len(_compact_json_bytes(result)) > MAX_MESSAGE_SIZE:
        result = {
            key: result[key]
            for key in tuple(_CODEX_NUMERIC_LIMITS) + ("codex_alert_revision",)
            if key in result
        }
        if isinstance(alerts, list):
            result["codex_alerts"] = []
    return result


def _bounded_task_detail(text: str) -> Optional[str]:
    prefix = []
    size = 0
    for character in text:
        try:
            encoded = character.encode("utf-8", errors="strict")
        except UnicodeEncodeError:
            return None
        if size + len(encoded) > MAX_TASK_DETAIL_BYTES:
            return "".join(prefix) + TASK_DETAIL_TRUNCATION_MARKER
        prefix.append(character)
        size += len(encoded)
    return text


def _task_detail_chunks(
    route: str,
    revision: int,
    text: str,
    max_size: int,
):
    if max_size < 1:
        return []
    if not text:
        empty_message = {
            "type": "task_detail",
            "route": route,
            "revision": revision,
            "index": 0,
            "count": 1,
            "text": "",
        }
        return [""] if len(_compact_json_bytes(empty_message)) <= max_size else []
    chunks = []
    characters = []
    chunk_size = 0
    byte_limit = min(MAX_TASK_DETAIL_CHUNK_BYTES, max_size)
    for character in text:
        encoded = character.encode("utf-8", errors="strict")
        if characters and chunk_size + len(encoded) > byte_limit:
            chunks.append("".join(characters))
            characters = []
            chunk_size = 0
        if len(encoded) > byte_limit:
            return []
        characters.append(character)
        chunk_size += len(encoded)
    if characters:
        chunks.append("".join(characters))
    return chunks


def task_detail_messages(
    status,
    max_size=MAX_MESSAGE_SIZE,
    retained_routes=None,
    active_alerts=None,
):
    """Return detail payloads with at most 12 KiB of reply plus a marker."""
    if not isinstance(status, Mapping):
        return []
    revision = _task_revision(status.get("codex_alert_revision"))
    alerts = status.get("codex_alerts")
    if not isinstance(alerts, list):
        return []
    messages = []
    normalized_alerts = (
        _normalized_active_alerts(status)
        if active_alerts is None
        else active_alerts
    )
    if retained_routes is None:
        compact_status = _watch_codex_status(status)
        retained_routes = {
            item["route"]
            for item in compact_status.get("codex_alerts", [])
        }
    allowed_routes = set(retained_routes)
    for alert in normalized_alerts:
        route = alert["route"]
        if route not in allowed_routes:
            continue
        reply = alert.get("last_reply")
        if not isinstance(reply, str):
            continue
        bounded = _bounded_task_detail(reply)
        if bounded is None:
            continue
        chunks = _task_detail_chunks(route, revision, bounded, max_size)
        count = len(chunks)
        messages.extend(
            {
                "type": "task_detail",
                "route": route,
                "revision": revision,
                "index": index,
                "count": count,
                "text": chunk,
            }
            for index, chunk in enumerate(chunks)
        )
    return messages


def _task_revision(value) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or not 0 <= value <= MAX_TASK_REVISION
    ):
        return 0
    return value


def _strict_bounded_text(value, limit):
    if not isinstance(value, str):
        return None
    try:
        value.encode("utf-8", errors="strict")
    except UnicodeEncodeError:
        return None
    return value[:limit]


def _watch_trae_status(status: Mapping[str, object]):
    raw_tasks = status.get("trae_tasks")
    source_tasks = raw_tasks if isinstance(raw_tasks, list) else []
    active_tasks = [
        item
        for item in source_tasks
        if isinstance(item, Mapping) and item.get("state") in ("pending", "running")
    ]
    process_state = status.get("trae_process_state")
    if process_state not in ("live", "off", "unknown"):
        process_state = "live" if status.get("trae_online") is True else "off"
    pending_count = _nonnegative_integer(status.get("trae_pending_count"))
    running_count = _nonnegative_integer(status.get("trae_running_count"))
    if "trae_pending_count" not in status:
        pending_count = sum(item.get("state") == "pending" for item in active_tasks)
    if "trae_running_count" not in status:
        running_count = sum(item.get("state") == "running" for item in active_tasks)
    pending_count = min(pending_count, MAX_TRAE_TASK_COUNT)
    running_count = min(running_count, MAX_TRAE_TASK_COUNT)

    result = {
        "type": "status",
        "source": "trae",
        "trae_online": status.get("trae_online") is True,
        "trae_model": _bounded_text(status.get("trae_model"), 64) or "--",
        "trae_effort": _bounded_text(status.get("trae_effort"), 32) or "--",
        "trae_revision": min(
            _nonnegative_integer(status.get("trae_revision")), MAX_TRAE_REVISION
        ),
        "trae_process_state": process_state,
        "trae_pending_count": pending_count,
        "trae_running_count": running_count,
        "trae_tasks_truncated": (
            status.get("trae_tasks_truncated") is True
            or len(active_tasks) > MAX_TRAE_TASKS
        ),
        "trae_tasks": [],
    }
    result["trae_tasks"] = [
        {
            "id": _bounded_text(item.get("id"), 32),
            "project": _bounded_text(item.get("project"), MAX_PROJECT_LENGTH),
            "title": _bounded_text(item.get("title"), MAX_TITLE_LENGTH),
            "detail": _bounded_text(item.get("detail"), MAX_DETAIL_LENGTH),
            "time": _bounded_text(item.get("time"), 8),
            "state": item.get("state"),
            "model": _bounded_text(item.get("model"), 64),
            "effort": _bounded_text(item.get("effort"), 32),
        }
        for item in active_tasks[:MAX_TRAE_TASKS]
    ]

    while len(_compact_json_bytes(result)) > MAX_TRAE_MESSAGE_SIZE and len(
        result["trae_tasks"]
    ) > 1:
        result["trae_tasks"].pop()
        result["trae_tasks_truncated"] = True
    while len(_compact_json_bytes(result)) > MAX_TRAE_MESSAGE_SIZE:
        shortened = False
        for task in reversed(result["trae_tasks"]):
            if task["detail"]:
                task["detail"] = task["detail"][:-1]
                result["trae_tasks_truncated"] = True
                shortened = True
                break
        if not shortened:
            if result["trae_tasks"]:
                result["trae_tasks_truncated"] = True
            result["trae_tasks"].clear()
            break
    return result


def _trae_fingerprint(payload: Mapping[str, object]) -> str:
    return hashlib.sha256(
        json.dumps(
            payload,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    ).hexdigest()


def _bounded_text(value: object, limit: int) -> str:
    return str(value).strip()[:limit] if isinstance(value, str) else ""


def _nonnegative_integer(value: object) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return 0
    if isinstance(value, float) and not math.isfinite(value):
        return 0
    return max(0, int(value))


def _compact_json_bytes(payload: Mapping[str, object]) -> bytes:
    return json.dumps(
        payload,
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")


def _watch_status(status):
    return _watch_codex_status(status)


class BleStatusTransport:
    def __init__(self, device_name: str = DEVICE_NAME):
        self.device_name = device_name
        self._client = None
        self._sequence = 0
        self._disconnected = asyncio.Event()
        self._voice_generation = None
        self._voice_notify_started = False
        self._voice_disabled = False
        self._pairing_responses = None
        self._pairing_notify_started = False
        self._pairing_transaction = 1
        self._input_generation = None
        self._input_notify_started = False

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

    @property
    def _input_queue(self):
        generation = self._input_generation
        return generation.queue if generation is not None else None

    @property
    def _input_consumer(self):
        generation = self._input_generation
        return generation.consumer if generation is not None else None

    @property
    def _input_ingress_pending(self):
        generation = self._input_generation
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
        try:
            await self.start_pairing_notifications()
        except BaseException:
            self._client = None
            if client.is_connected:
                await client.disconnect()
            raise
        return client.is_connected

    async def start_pairing_notifications(self) -> None:
        if not self.connected:
            raise ConnectionError("StopWatch BLE connection is not active")
        if self._pairing_notify_started:
            return
        loop = asyncio.get_running_loop()
        self._pairing_responses = asyncio.Queue(
            maxsize=PAIRING_RESPONSE_QUEUE_SIZE
        )

        def on_notification(_sender, data) -> None:
            frame = bytes(data)

            def deliver() -> None:
                queue = self._pairing_responses
                if queue is None:
                    return
                try:
                    queue.put_nowait(frame)
                except asyncio.QueueFull:
                    while not queue.empty():
                        try:
                            queue.get_nowait()
                        except asyncio.QueueEmpty:
                            break
                    queue.put_nowait(frame)

            try:
                loop.call_soon_threadsafe(deliver)
            except RuntimeError:
                pass

        await asyncio.wait_for(
            self._client.start_notify(PAIRING_TX_UUID, on_notification),
            timeout=GATT_OPERATION_TIMEOUT_SECONDS,
        )
        self._pairing_notify_started = True

    def _next_pairing_transaction(self) -> int:
        transaction_id = self._pairing_transaction
        self._pairing_transaction = (
            1 if transaction_id >= 0xFFFFFFFF else transaction_id + 1
        )
        return transaction_id

    async def _pairing_exchange(
        self,
        request: bytes,
        expected,
        *,
        timeout: float = GATT_OPERATION_TIMEOUT_SECONDS,
    ):
        if not self.connected:
            raise ConnectionError("StopWatch BLE connection is not active")
        await self.start_pairing_notifications()
        request_kind, transaction_id, _ = decode_pairing_frame(request)
        await asyncio.wait_for(
            self._client.write_gatt_char(
                PAIRING_CONTROL_UUID, request, response=True
            ),
            timeout=GATT_OPERATION_TIMEOUT_SECONDS,
        )
        queue = self._pairing_responses
        while True:
            response = await asyncio.wait_for(queue.get(), timeout=timeout)
            kind, response_transaction, payload = decode_pairing_frame(response)
            if response_transaction != transaction_id:
                continue
            if kind is WireMessageType.ERROR:
                raise PermissionError("watch rejected secure pairing operation")
            if kind not in expected:
                raise ValueError(
                    "watch returned an unexpected secure pairing response"
                )
            return kind, response_transaction, payload

    async def discover_watch(self) -> WatchDiscovery:
        transaction_id = self._next_pairing_transaction()
        _, _, payload = await self._pairing_exchange(
            encode_discovery_request(transaction_id),
            (WireMessageType.DISCOVERY_RESPONSE,),
        )
        watch_id, key_epoch, watch_identity_public = decode_discovery_response(
            payload
        )
        return WatchDiscovery(watch_id, key_epoch, watch_identity_public)

    async def request_pairing(self, request: BeginPairingRequest):
        _, transaction_id, payload = await self._pairing_exchange(
            encode_pairing_request(request),
            (WireMessageType.PAIRING_OFFER,),
        )
        return decode_pairing_offer(transaction_id, payload)

    async def confirm_pairing(self, transaction_id: int, accepted: bool):
        kind, response_transaction, payload = await self._pairing_exchange(
            encode_pairing_confirmation(transaction_id, accepted),
            (WireMessageType.CREDENTIAL, WireMessageType.REJECTED),
            timeout=PAIRING_CONFIRM_TIMEOUT_SECONDS,
        )
        if kind is WireMessageType.REJECTED:
            if payload:
                raise ValueError("watch rejection response is malformed")
            return None
        return decode_credential_envelope(response_transaction, payload)

    async def begin_authentication(self, request: AuthenticationRequest):
        _, transaction_id, payload = await self._pairing_exchange(
            encode_authentication_request(request),
            (WireMessageType.AUTHENTICATION_CHALLENGE,),
        )
        return decode_authentication_challenge(transaction_id, payload)

    async def authenticate(self, transaction_id: int, signature: bytes):
        kind, response_transaction, payload = await self._pairing_exchange(
            encode_authentication_proof(transaction_id, signature),
            (WireMessageType.AUTHENTICATION_OK, WireMessageType.CREDENTIAL),
            timeout=PAIRING_CONFIRM_TIMEOUT_SECONDS,
        )
        if kind is WireMessageType.AUTHENTICATION_OK:
            if payload:
                raise ValueError("authentication response is malformed")
            return None
        return decode_credential_envelope(response_transaction, payload)

    async def acknowledge_credential(
        self, transaction_id: int, proof: bytes
    ) -> None:
        kind, _, payload = await self._pairing_exchange(
            encode_credential_ack(transaction_id, proof),
            (WireMessageType.CREDENTIAL_ACK_OK,),
            timeout=PAIRING_CONFIRM_TIMEOUT_SECONDS,
        )
        if kind is not WireMessageType.CREDENTIAL_ACK_OK or payload:
            raise ValueError("credential acknowledgement response is malformed")

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

    async def enable_input_actions(self, ready_payload: bytes, handler) -> None:
        if not self.connected:
            raise ConnectionError("StopWatch BLE connection is not active")
        if self._input_generation is not None:
            raise RuntimeError("input action generation is already active")
        if self._input_notify_started:
            raise RuntimeError("input action notification cleanup is incomplete")

        loop = asyncio.get_running_loop()
        queue = asyncio.Queue(maxsize=INPUT_NOTIFICATION_QUEUE_SIZE)
        generation = _InputGeneration(
            queue,
            asyncio.create_task(self._consume_input(queue, handler)),
            _InputIngressGate(),
        )
        self._input_generation = generation

        def on_notification(_sender, data) -> None:
            payload = bytes(data)
            with generation.ingress.lock:
                if (
                    not generation.ingress.enabled
                    or generation.ingress.pending
                    >= INPUT_NOTIFICATION_QUEUE_SIZE
                ):
                    return
                generation.ingress.pending += 1
            try:
                loop.call_soon_threadsafe(
                    self._deliver_input_payload, generation, payload
                )
            except RuntimeError:
                self._release_input_ingress(generation)

        # Own notification cleanup before entering the cancellable GATT call.
        self._input_notify_started = True
        try:
            await asyncio.wait_for(
                self._client.start_notify(INPUT_TX_UUID, on_notification),
                timeout=GATT_OPERATION_TIMEOUT_SECONDS,
            )
            await asyncio.wait_for(
                self._client.write_gatt_char(
                    INPUT_CONTROL_UUID, ready_payload, response=True
                ),
                timeout=GATT_OPERATION_TIMEOUT_SECONDS,
            )
        except BaseException:
            consumer = self._retire_input_generation(generation)
            try:
                if self.connected and self._input_notify_started:
                    try:
                        await asyncio.wait_for(
                            self._client.stop_notify(INPUT_TX_UUID),
                            timeout=GATT_OPERATION_TIMEOUT_SECONDS,
                        )
                    except Exception:
                        pass
                    else:
                        self._input_notify_started = False
            finally:
                if consumer is not None:
                    await _await_cancelled_task(consumer)
            raise

    async def disable_input_actions(self) -> None:
        consumer = self._retire_input_generation()
        failure = None
        try:
            if self.connected and self._input_notify_started:
                try:
                    await asyncio.wait_for(
                        self._client.stop_notify(INPUT_TX_UUID),
                        timeout=GATT_OPERATION_TIMEOUT_SECONDS,
                    )
                except Exception as error:
                    failure = error
                else:
                    self._input_notify_started = False
        finally:
            if consumer is not None:
                await _await_cancelled_task(consumer)
        if failure is not None:
            raise failure

    def _retire_input_generation(self, expected=None):
        generation = self._input_generation
        if generation is None or (
            expected is not None and generation is not expected
        ):
            return None
        with generation.ingress.lock:
            generation.ingress.enabled = False
        if self._input_generation is generation:
            self._input_generation = None
        if not generation.consumer.done():
            generation.consumer.cancel()
        return generation.consumer

    def _deliver_input_payload(
        self, generation: _InputGeneration, payload: bytes
    ) -> None:
        with generation.ingress.lock:
            enabled = generation.ingress.enabled
        if not enabled or self._input_generation is not generation:
            self._release_input_ingress(generation)
            return
        try:
            generation.queue.put_nowait(payload)
        except asyncio.QueueFull:
            self._release_input_ingress(generation)

    @staticmethod
    def _release_input_ingress(generation: _InputGeneration) -> None:
        with generation.ingress.lock:
            if generation.ingress.pending:
                generation.ingress.pending -= 1

    async def _consume_input(self, queue: asyncio.Queue, handler) -> None:
        generation = self._input_generation
        while True:
            payload = await queue.get()
            try:
                await handler(payload)
            except asyncio.CancelledError:
                raise
            except Exception:
                LOGGER.exception("Input action handler failed")
            finally:
                if generation is not None:
                    self._release_input_ingress(generation)

    async def disconnect(self) -> None:
        client = self._client
        if client and client.is_connected:
            if self._pairing_notify_started:
                try:
                    await asyncio.wait_for(
                        client.stop_notify(PAIRING_TX_UUID),
                        timeout=GATT_OPERATION_TIMEOUT_SECONDS,
                    )
                except Exception:
                    pass
            await client.disconnect()
        self._client = None
        self._pairing_notify_started = False
        self._pairing_responses = None

    async def send_json(self, payload: str, message_type: int) -> None:
        if not self.connected:
            raise ConnectionError("StopWatch BLE connection is not active")
        sequence = self._sequence
        self._sequence = (self._sequence + 1) & 0xFFFF
        frames = encode_message(
            payload.encode("utf-8"),
            message_type=message_type,
            sequence=sequence,
            max_write_size=self._status_write_size(),
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

    def _status_write_size(self) -> int:
        client = self._client
        services = getattr(client, "services", None)
        get_characteristic = getattr(services, "get_characteristic", None)
        if get_characteristic is None:
            return SAFE_WRITE_SIZE
        try:
            characteristic = get_characteristic(STATUS_RX_UUID)
            capacity = getattr(
                characteristic,
                "max_write_without_response_size",
                None,
            )
        except Exception:
            return SAFE_WRITE_SIZE
        if isinstance(capacity, bool) or not isinstance(capacity, int):
            return SAFE_WRITE_SIZE
        if capacity < SAFE_WRITE_SIZE:
            return SAFE_WRITE_SIZE
        return min(capacity, MAX_STATUS_WRITE_SIZE)
