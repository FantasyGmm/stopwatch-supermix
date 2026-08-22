"""Authenticated, bounded Agent-side Codex pet transfer state machine."""

from __future__ import annotations

import asyncio
import dataclasses
import enum
import inspect
import secrets
from typing import Callable, Optional

from pet_package import SCHEMA_VERSION, PetPackage, parse_package
from pet_protocol import (
    CHUNK_HEADER,
    MAX_CHUNK_BYTES,
    PREFIX,
    ErrorReason,
    MessageKind,
    PetSource,
    PetStatus,
    StatusReplayGuard,
    TAG_BYTES,
    decode_status,
    encode_begin,
    encode_cancel,
    encode_chunk,
    encode_commit,
    encode_query,
    encode_restore,
)


ATT_WRITE_OVERHEAD = 3
CHUNK_PROTOCOL_OVERHEAD = (
    ATT_WRITE_OVERHEAD + PREFIX.size + CHUNK_HEADER.size + TAG_BYTES
)
DEFAULT_ACKNOWLEDGEMENT_TIMEOUT = 5.0
DEFAULT_PROGRESS_RETRIES = 1
STATUS_QUEUE_SIZE = 8


class PetCapability(enum.Enum):
    OFFLINE = "offline"
    UNSUPPORTED = "unsupported"
    AUTHENTICATING = "authenticating"
    READY = "ready"


@dataclasses.dataclass(frozen=True)
class PetWatchState:
    watch_id: Optional[bytes]
    capability: PetCapability
    source: str
    schema_min: int
    schema_max: int
    generation: int
    active_hash: bytes
    usable: bool


@dataclasses.dataclass(frozen=True)
class PetProgress:
    request_id: str
    phase: str
    acknowledged_bytes: int
    total_bytes: int
    error_code: str = ""
    required_bytes: int = 0
    available_bytes: int = 0


class PetOperation(enum.Enum):
    UPLOAD = "upload"
    CANCEL = "cancel"
    RESTORE = "restore"


@dataclasses.dataclass(frozen=True)
class PetRequest:
    request_id: str
    operation: PetOperation
    package: Optional[PetPackage]
    callback: Callable[[PetProgress], None]


class PetUploadError(RuntimeError):
    """A redacted pet-channel operation failure."""

    error_code = "transfer_failed"
    required_bytes = 0
    available_bytes = 0


class PetUnsupportedError(PetUploadError):
    error_code = "unsupported"


class PetBusyError(PetUploadError):
    error_code = "busy"


class PetCancelledError(PetUploadError):
    error_code = "cancelled"


class PetCommitAmbiguousError(PetUploadError):
    error_code = "commit_ambiguous"


@dataclasses.dataclass
class _Operation:
    request_id: str
    transaction_id: int
    nonce: bytes
    statuses: asyncio.Queue
    replay: StatusReplayGuard
    done: asyncio.Event
    sequence: int = 0
    cancel_sent: bool = False
    commit_sent: bool = False


class PetUploader:
    def __init__(
        self,
        transport,
        key: bytes,
        key_epoch: int,
        nonce_factory: Callable[[], bytes],
        *,
        watch_id: Optional[bytes] = None,
        acknowledgement_timeout: float = DEFAULT_ACKNOWLEDGEMENT_TIMEOUT,
        progress_retries: int = DEFAULT_PROGRESS_RETRIES,
        transaction_id_factory: Optional[Callable[[], int]] = None,
    ) -> None:
        if not isinstance(key, bytes) or len(key) != 16 or not any(key):
            raise PetUploadError("authenticated pet key is invalid")
        if (
            isinstance(key_epoch, bool)
            or not isinstance(key_epoch, int)
            or not 1 <= key_epoch <= 0xFFFFFFFF
        ):
            raise PetUploadError("authenticated pet key epoch is invalid")
        if watch_id is not None and (
            not isinstance(watch_id, bytes)
            or len(watch_id) != 16
            or not any(watch_id)
        ):
            raise PetUploadError("watch identity is invalid")
        if acknowledgement_timeout <= 0:
            raise PetUploadError("pet acknowledgement timeout is invalid")
        if (
            isinstance(progress_retries, bool)
            or not isinstance(progress_retries, int)
            or not 0 <= progress_retries <= 3
        ):
            raise PetUploadError("pet progress retry bound is invalid")
        self._transport = transport
        self._key = key
        self._key_epoch = key_epoch
        self._nonce_factory = nonce_factory
        self._watch_id = watch_id
        self._acknowledgement_timeout = acknowledgement_timeout
        self._progress_retries = progress_retries
        self._transaction_id_factory = (
            transaction_id_factory or self._random_transaction_id
        )
        self._operation_lock = asyncio.Lock()
        self._write_lock = asyncio.Lock()
        self._active: Optional[_Operation] = None
        self._watch_state: Optional[PetWatchState] = None
        self._notifications_enabled = False
        self._notification_generation = 0

    @property
    def key_epoch(self) -> int:
        return self._key_epoch

    @property
    def watch_state(self) -> Optional[PetWatchState]:
        return self._watch_state

    async def query(self) -> PetWatchState:
        async with self._operation_lock:
            self._ensure_connected()
            await self._ensure_notifications()
            operation = self._begin_operation()
            try:
                frame = encode_query(
                    self._key,
                    operation.nonce,
                    self._next_sequence(operation),
                    operation.transaction_id,
                )
                await self._write(frame)
                status = await self._wait_status(operation)
                if status.kind != MessageKind.STATE:
                    raise PetUploadError("watch returned conflicting pet state")
                state = self._state_from_status(status)
                self._watch_state = state
                return state
            finally:
                self._finish_operation(operation)

    async def upload(
        self,
        package: PetPackage,
        progress,
        *,
        request_id: Optional[str] = None,
    ) -> PetWatchState:
        canonical = self._canonical_package(package)
        if self._watch_state is None:
            await self.query()
        if not (
            self._watch_state.schema_min
            <= SCHEMA_VERSION
            <= self._watch_state.schema_max
        ):
            raise PetUnsupportedError("Firmware update required")
        async with self._operation_lock:
            self._ensure_connected()
            await self._ensure_notifications()
            operation = self._begin_operation(request_id)
            commit_sent = False
            completed = False
            try:
                begin = encode_begin(
                    self._key,
                    operation.nonce,
                    self._next_sequence(operation),
                    operation.transaction_id,
                    SCHEMA_VERSION,
                    canonical.width,
                    canonical.height,
                    len(canonical.frames),
                    len(canonical.encoded),
                    canonical.sha256,
                )
                await self._write(begin)
                initial = await self._wait_status(operation)
                self._require_progress(initial, 0, len(canonical.encoded))
                self._publish(
                    progress,
                    PetProgress(
                        operation.request_id,
                        "sending",
                        0,
                        len(canonical.encoded),
                    ),
                )

                chunk_size = self._chunk_size()
                offset = 0
                while offset < len(canonical.encoded):
                    data = canonical.encoded[offset : offset + chunk_size]
                    frame = encode_chunk(
                        self._key,
                        operation.nonce,
                        self._next_sequence(operation),
                        operation.transaction_id,
                        offset,
                        data,
                    )
                    expected = offset + len(data)
                    status = await self._write_chunk_and_wait(
                        operation, frame
                    )
                    self._require_progress(
                        status, expected, len(canonical.encoded)
                    )
                    offset = expected
                    self._publish(
                        progress,
                        PetProgress(
                            operation.request_id,
                            "sending",
                            offset,
                            len(canonical.encoded),
                        ),
                    )

                self._publish(
                    progress,
                    PetProgress(
                        operation.request_id,
                        "verifying",
                        len(canonical.encoded),
                        len(canonical.encoded),
                    ),
                )
                commit = encode_commit(
                    self._key,
                    operation.nonce,
                    self._next_sequence(operation),
                    operation.transaction_id,
                )
                commit_sent = True
                operation.commit_sent = True
                try:
                    await self._write(commit)
                    status = await self._wait_status(operation)
                except (asyncio.TimeoutError, ConnectionError, OSError) as error:
                    raise PetCommitAmbiguousError(
                        "pet Commit result is ambiguous"
                    ) from error
                if status.kind != MessageKind.COMMITTED:
                    raise PetCommitAmbiguousError(
                        "pet Commit result is ambiguous"
                    )
                committed = status.committed
                if committed.active_sha256 != canonical.sha256:
                    raise PetUploadError("watch committed a conflicting pet hash")
                previous = self._watch_state
                state = PetWatchState(
                    self._watch_id,
                    PetCapability.READY,
                    "custom",
                    previous.schema_min if previous is not None else SCHEMA_VERSION,
                    previous.schema_max if previous is not None else SCHEMA_VERSION,
                    committed.generation,
                    committed.active_sha256,
                    True,
                )
                self._publish(
                    progress,
                    PetProgress(
                        operation.request_id,
                        "applying",
                        len(canonical.encoded),
                        len(canonical.encoded),
                    ),
                )
                self._watch_state = state
                self._publish(
                    progress,
                    PetProgress(
                        operation.request_id,
                        "installed",
                        len(canonical.encoded),
                        len(canonical.encoded),
                    ),
                )
                completed = True
                return state
            except asyncio.TimeoutError as error:
                if not commit_sent:
                    await self._cancel_after_failure(operation)
                raise PetUploadError("pet acknowledgement timed out") from error
            except PetCancelledError:
                raise
            except Exception:
                if not commit_sent:
                    await self._cancel_after_failure(operation)
                raise
            finally:
                if not completed and not commit_sent and operation.cancel_sent:
                    pass
                self._finish_operation(operation)

    async def cancel(self, request_id: str) -> None:
        operation = self._active
        if operation is None or operation.request_id != request_id:
            return
        if operation.commit_sent:
            await operation.done.wait()
            return
        if operation.cancel_sent:
            await operation.done.wait()
            return
        operation.cancel_sent = True
        frame = encode_cancel(
            self._key,
            operation.nonce,
            self._next_sequence(operation),
            operation.transaction_id,
        )
        await self._write(frame)
        await operation.done.wait()

    async def restore(
        self,
        progress,
        *,
        request_id: Optional[str] = None,
    ) -> PetWatchState:
        resolved_request_id = request_id or secrets.token_hex(8)
        try:
            return await self._restore_once(
                progress, request_id=resolved_request_id
            )
        except asyncio.CancelledError:
            raise
        except PetCancelledError:
            raise
        except Exception:
            try:
                state = await self.query()
            except asyncio.CancelledError:
                raise
            except Exception as reconciliation_error:
                previous = self._watch_state
                self._watch_state = PetWatchState(
                    self._watch_id,
                    PetCapability.OFFLINE,
                    previous.source if previous is not None else "built-in",
                    previous.schema_min if previous is not None else 0,
                    previous.schema_max if previous is not None else 0,
                    previous.generation if previous is not None else 0,
                    previous.active_hash if previous is not None else bytes(32),
                    False,
                )
                raise PetUploadError(
                    "restore result could not be reconciled"
                ) from reconciliation_error
            if state.source == "built-in" and not any(state.active_hash):
                self._publish(
                    progress,
                    PetProgress(resolved_request_id, "installed", 0, 0),
                )
            return state

    async def _restore_once(
        self,
        progress,
        *,
        request_id: Optional[str] = None,
    ) -> PetWatchState:
        async with self._operation_lock:
            self._ensure_connected()
            await self._ensure_notifications()
            operation = self._begin_operation(request_id)
            try:
                self._publish(
                    progress,
                    PetProgress(operation.request_id, "applying", 0, 0),
                )
                frame = encode_restore(
                    self._key,
                    operation.nonce,
                    self._next_sequence(operation),
                    operation.transaction_id,
                )
                await self._write(frame)
                status = await self._wait_status(operation)
                if status.kind != MessageKind.STATE:
                    raise PetUploadError("watch returned conflicting restore state")
                state = self._state_from_status(status)
                if state.source != "built-in" or any(state.active_hash):
                    raise PetUploadError("watch restore state is conflicting")
                self._watch_state = state
                self._publish(
                    progress,
                    PetProgress(operation.request_id, "installed", 0, 0),
                )
                return state
            finally:
                self._finish_operation(operation)

    async def close(self) -> None:
        try:
            operation = self._active
            if operation is not None and not operation.cancel_sent:
                try:
                    await self.cancel(operation.request_id)
                except Exception:
                    pass
        finally:
            self._notification_generation += 1
            self._notifications_enabled = False
            try:
                await self._transport.disable_pet_notifications()
            except Exception:
                pass

    async def _ensure_notifications(self) -> None:
        if self._notifications_enabled:
            return
        generation = self._notification_generation + 1
        self._notification_generation = generation

        async def handle(payload: bytes) -> None:
            if generation != self._notification_generation:
                return
            operation = self._active
            if operation is None:
                return
            try:
                status = decode_status(self._key, bytes(payload))
                if (
                    status.transaction_id != operation.transaction_id
                    or status.exchange_nonce != operation.nonce
                ):
                    return
                if not operation.replay.accept(status):
                    raise PetUploadError("stale or replayed pet status")
                operation.statuses.put_nowait(status)
            except asyncio.QueueFull:
                try:
                    operation.statuses.get_nowait()
                except asyncio.QueueEmpty:
                    pass
                try:
                    operation.statuses.put_nowait(
                        PetUploadError("pet status queue overflow")
                    )
                except asyncio.QueueFull:
                    pass
            except Exception as error:
                try:
                    operation.statuses.put_nowait(error)
                except asyncio.QueueFull:
                    pass

        try:
            await self._transport.enable_pet_notifications(handle)
        except PetUnsupportedError:
            raise
        except Exception as error:
            if not self._connected():
                raise PetUploadError("watch is offline") from error
            raise PetUnsupportedError("Firmware update required") from error
        self._notifications_enabled = True

    def _begin_operation(self, request_id: Optional[str] = None) -> _Operation:
        if self._active is not None:
            raise PetBusyError("pet channel is busy")
        transaction_id = self._transaction_id_factory()
        if (
            isinstance(transaction_id, bool)
            or not isinstance(transaction_id, int)
            or not 1 <= transaction_id <= 0xFFFFFFFF
        ):
            raise PetUploadError("pet transaction identifier is invalid")
        nonce = self._nonce_factory()
        if not isinstance(nonce, bytes) or len(nonce) != 8 or not any(nonce):
            raise PetUploadError("pet transaction nonce is invalid")
        replay = StatusReplayGuard()
        replay.set_active_nonce(nonce)
        operation = _Operation(
            request_id or "{:08x}".format(transaction_id),
            transaction_id,
            nonce,
            asyncio.Queue(maxsize=STATUS_QUEUE_SIZE),
            replay,
            asyncio.Event(),
        )
        self._active = operation
        return operation

    def _finish_operation(self, operation: _Operation) -> None:
        operation.replay.retire()
        if self._active is operation:
            self._active = None
        operation.done.set()

    async def _write(self, frame: bytes) -> None:
        self._ensure_connected()
        async with self._write_lock:
            await self._transport.write_pet_control(frame)

    async def _write_chunk_and_wait(
        self, operation: _Operation, frame: bytes
    ) -> PetStatus:
        attempts = 0
        while True:
            await self._write(frame)
            try:
                return await self._wait_status(operation)
            except asyncio.TimeoutError:
                if attempts >= self._progress_retries:
                    raise
                attempts += 1

    async def _wait_status(self, operation: _Operation) -> PetStatus:
        item = await asyncio.wait_for(
            operation.statuses.get(), self._acknowledgement_timeout
        )
        if isinstance(item, Exception):
            raise item
        if item.kind == MessageKind.ERROR:
            error = item.error
            if error.reason == ErrorReason.BUSY:
                raise PetBusyError("watch pet channel is busy")
            failure = PetUploadError(
                "watch rejected pet operation: {}".format(
                    error.reason.name.lower()
                )
            )
            failure.error_code = error.reason.name.lower()
            failure.required_bytes = error.required_bytes
            failure.available_bytes = error.available_bytes
            raise failure
        if item.kind == MessageKind.CANCELLED:
            raise PetCancelledError("pet upload was cancelled")
        return item

    async def _cancel_after_failure(self, operation: _Operation) -> None:
        if operation.cancel_sent or not self._connected():
            return
        operation.cancel_sent = True
        try:
            frame = encode_cancel(
                self._key,
                operation.nonce,
                self._next_sequence(operation),
                operation.transaction_id,
            )
            await self._write(frame)
        except Exception:
            pass

    @staticmethod
    def _require_progress(
        status: PetStatus, acknowledged: int, total: int
    ) -> None:
        if status.kind != MessageKind.PROGRESS:
            raise PetUploadError("watch returned conflicting pet progress")
        progress = status.progress
        if (
            progress.acknowledged_bytes != acknowledged
            or progress.total_bytes != total
        ):
            raise PetUploadError("watch returned conflicting pet progress")

    def _state_from_status(self, status: PetStatus) -> PetWatchState:
        state = status.state
        source = "custom" if state.source == PetSource.CUSTOM else "built-in"
        return PetWatchState(
            self._watch_id,
            PetCapability.READY,
            source,
            state.schema_min,
            state.schema_max,
            state.generation,
            state.active_sha256,
            state.usable,
        )

    def _chunk_size(self) -> int:
        value = getattr(self._transport, "negotiated_mtu", 23)
        if callable(value):
            value = value()
        if isinstance(value, bool) or not isinstance(value, int):
            value = 23
        capacity = value - CHUNK_PROTOCOL_OVERHEAD
        if capacity < 1:
            raise PetUploadError("negotiated BLE MTU is too small")
        return min(MAX_CHUNK_BYTES, capacity)

    def _ensure_connected(self) -> None:
        if not self._connected():
            raise PetUploadError("watch is offline")

    def _connected(self) -> bool:
        value = getattr(self._transport, "connected", False)
        return bool(value() if callable(value) else value)

    @staticmethod
    def _canonical_package(package: PetPackage) -> PetPackage:
        if not isinstance(package, PetPackage):
            raise PetUploadError("pet package is invalid")
        try:
            parsed = parse_package(package.encoded)
        except Exception as error:
            raise PetUploadError("pet package is invalid") from error
        if parsed != package:
            raise PetUploadError("pet package is not canonical")
        return parsed

    @staticmethod
    def _publish(callback, state: PetProgress) -> None:
        if callback is None:
            return
        try:
            result = callback(state)
            if inspect.isawaitable(result):
                asyncio.create_task(result)
        except Exception:
            pass

    @staticmethod
    def _next_sequence(operation: _Operation) -> int:
        if operation.sequence == 0xFFFFFFFF:
            raise PetUploadError("pet control sequence exhausted")
        operation.sequence += 1
        return operation.sequence

    @staticmethod
    def _random_transaction_id() -> int:
        value = 0
        while value == 0:
            value = secrets.randbits(32)
        return value
