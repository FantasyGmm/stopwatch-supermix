"""Bounded authenticated wire protocol for Codex pet transfers."""

import dataclasses
import enum
import struct
from collections import deque
from typing import Deque, Optional, Union

from cryptography.exceptions import InvalidTag

from voice_input_crypto import (
    PET_CONTROL_DOMAIN,
    PET_STATUS_DOMAIN,
    TAG_BYTES,
    decrypt,
    encrypt,
)


PROTOCOL_VERSION = 1
MAX_CHUNK_BYTES = 480
MAX_PACKAGE_BYTES = 256 * 1024
COMPLETED_TRANSACTION_TOMBSTONES = 8
PREFIX = struct.Struct("<BBI8s")
QUERY_BODY = struct.Struct("<I")
BEGIN_BODY = struct.Struct("<IHHHHI32s")
CHUNK_HEADER = struct.Struct("<IIH")
STATE_BODY = struct.Struct("<IBBHHHI32s")
PROGRESS_BODY = struct.Struct("<III")
COMMITTED_BODY = struct.Struct("<II32s")
ERROR_BODY = struct.Struct("<IHHII")
MAX_FRAME_BYTES = PREFIX.size + CHUNK_HEADER.size + MAX_CHUNK_BYTES + TAG_BYTES


class PetProtocolError(ValueError):
    """Raised when a pet frame or logical field fails closed validation."""


class MessageKind(enum.IntEnum):
    QUERY = 0x01
    BEGIN = 0x02
    CHUNK = 0x03
    COMMIT = 0x04
    CANCEL = 0x05
    RESTORE = 0x06
    STATE = 0x81
    PROGRESS = 0x82
    COMMITTED = 0x83
    CANCELLED = 0x84
    ERROR = 0xFF


class PetSource(enum.IntEnum):
    BUILT_IN = 0
    CUSTOM = 1


class ErrorReason(enum.IntEnum):
    INVALID_MESSAGE = 1
    UNAUTHORIZED = 2
    BUSY = 3
    UNSUPPORTED_SCHEMA = 4
    INVALID_METADATA = 5
    INVALID_OFFSET = 6
    REPLAY = 7
    INSUFFICIENT_STORAGE = 8
    HASH_MISMATCH = 9
    IO_FAILURE = 10
    TIMEOUT = 11
    UNUSABLE_PACKAGE = 12
    TOO_LARGE = 13
    INTERNAL = 14


class ReplayDecision(enum.IntEnum):
    REJECTED = 0
    ACCEPTED = 1
    DUPLICATE = 2


@dataclasses.dataclass(frozen=True)
class Query:
    transaction_id: int


@dataclasses.dataclass(frozen=True)
class Begin:
    transaction_id: int
    schema: int
    width: int
    height: int
    frame_count: int
    package_length: int
    sha256: bytes


@dataclasses.dataclass(frozen=True)
class Chunk:
    transaction_id: int
    offset: int
    data: bytes


@dataclasses.dataclass(frozen=True)
class Transaction:
    transaction_id: int


ControlPayload = Union[Query, Begin, Chunk, Transaction]


@dataclasses.dataclass(frozen=True)
class PetControl:
    kind: MessageKind
    payload: ControlPayload
    sequence: int = 0
    exchange_nonce: bytes = b""

    @property
    def transaction_id(self) -> int:
        return self.payload.transaction_id

    @property
    def query(self) -> Query:
        if not isinstance(self.payload, Query):
            raise PetProtocolError("control is not Query")
        return self.payload

    @property
    def begin(self) -> Begin:
        if not isinstance(self.payload, Begin):
            raise PetProtocolError("control is not Begin")
        return self.payload

    @property
    def chunk(self) -> Chunk:
        if not isinstance(self.payload, Chunk):
            raise PetProtocolError("control is not Chunk")
        return self.payload

    @property
    def transaction(self) -> Transaction:
        if not isinstance(self.payload, Transaction):
            raise PetProtocolError("control has no terminal transaction body")
        return self.payload


@dataclasses.dataclass(frozen=True)
class StateStatus:
    transaction_id: int
    source: PetSource
    usable: bool
    schema_min: int
    schema_max: int
    generation: int
    active_sha256: bytes


@dataclasses.dataclass(frozen=True)
class ProgressStatus:
    transaction_id: int
    acknowledged_bytes: int
    total_bytes: int


@dataclasses.dataclass(frozen=True)
class CommittedStatus:
    transaction_id: int
    generation: int
    active_sha256: bytes


@dataclasses.dataclass(frozen=True)
class CancelledStatus:
    transaction_id: int


@dataclasses.dataclass(frozen=True)
class ErrorStatus:
    transaction_id: int
    reason: ErrorReason
    required_bytes: int
    available_bytes: int


StatusPayload = Union[
    StateStatus,
    ProgressStatus,
    CommittedStatus,
    CancelledStatus,
    ErrorStatus,
]


@dataclasses.dataclass(frozen=True)
class PetStatus:
    kind: MessageKind
    payload: StatusPayload
    sequence: int = 0
    exchange_nonce: bytes = b""

    @property
    def transaction_id(self) -> int:
        return self.payload.transaction_id

    @property
    def state(self) -> StateStatus:
        if not isinstance(self.payload, StateStatus):
            raise PetProtocolError("status is not State")
        return self.payload

    @property
    def progress(self) -> ProgressStatus:
        if not isinstance(self.payload, ProgressStatus):
            raise PetProtocolError("status is not Progress")
        return self.payload

    @property
    def committed(self) -> CommittedStatus:
        if not isinstance(self.payload, CommittedStatus):
            raise PetProtocolError("status is not Committed")
        return self.payload

    @property
    def cancelled(self) -> CancelledStatus:
        if not isinstance(self.payload, CancelledStatus):
            raise PetProtocolError("status is not Cancelled")
        return self.payload

    @property
    def error(self) -> ErrorStatus:
        if not isinstance(self.payload, ErrorStatus):
            raise PetProtocolError("status is not Error")
        return self.payload


CONTROL_KINDS = frozenset(
    {
        MessageKind.QUERY,
        MessageKind.BEGIN,
        MessageKind.CHUNK,
        MessageKind.COMMIT,
        MessageKind.CANCEL,
        MessageKind.RESTORE,
    }
)
STATUS_KINDS = frozenset(
    {
        MessageKind.STATE,
        MessageKind.PROGRESS,
        MessageKind.COMMITTED,
        MessageKind.CANCELLED,
        MessageKind.ERROR,
    }
)


def _uint(name: str, value: int, minimum: int, maximum: int) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or not minimum <= value <= maximum
    ):
        raise PetProtocolError(f"{name} must be {minimum}..{maximum}")
    return value


def _nonce(value: bytes) -> bytes:
    if not isinstance(value, bytes) or len(value) != 8 or not any(value):
        raise PetProtocolError("exchange nonce must be 8 nonzero bytes")
    return value


def _bytes(name: str, value: bytes, exact: Optional[int] = None) -> bytes:
    if not isinstance(value, bytes) or exact is not None and len(value) != exact:
        suffix = "bytes" if exact is None else f"exactly {exact} bytes"
        raise PetProtocolError(f"{name} must be {suffix}")
    return value


def _kind(value: MessageKind, allowed) -> MessageKind:
    if isinstance(value, bool):
        raise PetProtocolError("unknown pet message kind")
    try:
        converted = MessageKind(value)
    except (TypeError, ValueError):
        raise PetProtocolError("unknown pet message kind") from None
    if converted not in allowed:
        raise PetProtocolError("pet message kind is on the wrong channel")
    return converted


def _source(value: PetSource) -> PetSource:
    if isinstance(value, bool):
        raise PetProtocolError("unknown pet source")
    try:
        return PetSource(value)
    except (TypeError, ValueError):
        raise PetProtocolError("unknown pet source") from None


def _reason(value: ErrorReason) -> ErrorReason:
    if isinstance(value, bool):
        raise PetProtocolError("unknown pet error reason")
    try:
        return ErrorReason(value)
    except (TypeError, ValueError):
        raise PetProtocolError("unknown pet error reason") from None


def _transaction_id(value: int) -> int:
    return _uint("transaction_id", value, 1, 0xFFFFFFFF)


def _sequence(value: int) -> int:
    return _uint("sequence", value, 1, 0xFFFFFFFF)


def _control_body(control: PetControl) -> bytes:
    if not isinstance(control, PetControl):
        raise PetProtocolError("control must be PetControl")
    kind = _kind(control.kind, CONTROL_KINDS)
    payload = control.payload
    if kind == MessageKind.QUERY:
        if not isinstance(payload, Query):
            raise PetProtocolError("Query body type mismatch")
        return QUERY_BODY.pack(_transaction_id(payload.transaction_id))
    if kind == MessageKind.BEGIN:
        if not isinstance(payload, Begin):
            raise PetProtocolError("Begin body type mismatch")
        digest = _bytes("sha256", payload.sha256, 32)
        if not any(digest):
            raise PetProtocolError("sha256 must be nonzero")
        return BEGIN_BODY.pack(
            _transaction_id(payload.transaction_id),
            _uint("schema", payload.schema, 1, 0xFFFF),
            _uint("width", payload.width, 1, 0xFFFF),
            _uint("height", payload.height, 1, 0xFFFF),
            _uint("frame_count", payload.frame_count, 1, 24),
            _uint("package_length", payload.package_length, 1, MAX_PACKAGE_BYTES),
            digest,
        )
    if kind == MessageKind.CHUNK:
        if not isinstance(payload, Chunk):
            raise PetProtocolError("Chunk body type mismatch")
        data = _bytes("data", payload.data)
        if not 1 <= len(data) <= MAX_CHUNK_BYTES:
            raise PetProtocolError(f"data must be 1..{MAX_CHUNK_BYTES} bytes")
        offset = _uint("offset", payload.offset, 0, 0xFFFFFFFF)
        if len(data) > 0xFFFFFFFF - offset:
            raise PetProtocolError("chunk offset and length overflow")
        return CHUNK_HEADER.pack(
            _transaction_id(payload.transaction_id), offset, len(data)
        ) + data
    if not isinstance(payload, Transaction):
        raise PetProtocolError("transaction body type mismatch")
    return QUERY_BODY.pack(_transaction_id(payload.transaction_id))


def _status_body(status: PetStatus) -> bytes:
    if not isinstance(status, PetStatus):
        raise PetProtocolError("status must be PetStatus")
    kind = _kind(status.kind, STATUS_KINDS)
    payload = status.payload
    if kind == MessageKind.STATE:
        if not isinstance(payload, StateStatus):
            raise PetProtocolError("State body type mismatch")
        source = _source(payload.source)
        if type(payload.usable) is not bool:
            raise PetProtocolError("usable must be a boolean")
        schema_min = _uint("schema_min", payload.schema_min, 1, 0xFFFF)
        schema_max = _uint("schema_max", payload.schema_max, 1, 0xFFFF)
        if schema_min > schema_max:
            raise PetProtocolError("schema_min must not exceed schema_max")
        digest = _bytes("active_sha256", payload.active_sha256, 32)
        if source == PetSource.BUILT_IN and any(digest):
            raise PetProtocolError("built-in State hash must be zero")
        if source == PetSource.CUSTOM and not any(digest):
            raise PetProtocolError("custom State hash must be nonzero")
        return STATE_BODY.pack(
            _transaction_id(payload.transaction_id),
            int(source),
            int(payload.usable),
            schema_min,
            schema_max,
            0,
            _uint("generation", payload.generation, 0, 0xFFFFFFFF),
            digest,
        )
    if kind == MessageKind.PROGRESS:
        if not isinstance(payload, ProgressStatus):
            raise PetProtocolError("Progress body type mismatch")
        acknowledged = _uint(
            "acknowledged_bytes", payload.acknowledged_bytes, 0, MAX_PACKAGE_BYTES
        )
        total = _uint("total_bytes", payload.total_bytes, 1, MAX_PACKAGE_BYTES)
        if acknowledged > total:
            raise PetProtocolError("acknowledged_bytes must not exceed total_bytes")
        return PROGRESS_BODY.pack(
            _transaction_id(payload.transaction_id), acknowledged, total
        )
    if kind == MessageKind.COMMITTED:
        if not isinstance(payload, CommittedStatus):
            raise PetProtocolError("Committed body type mismatch")
        digest = _bytes("active_sha256", payload.active_sha256, 32)
        if not any(digest):
            raise PetProtocolError("committed hash must be nonzero")
        return COMMITTED_BODY.pack(
            _transaction_id(payload.transaction_id),
            _uint("generation", payload.generation, 1, 0xFFFFFFFF),
            digest,
        )
    if kind == MessageKind.CANCELLED:
        if not isinstance(payload, CancelledStatus):
            raise PetProtocolError("Cancelled body type mismatch")
        return QUERY_BODY.pack(_transaction_id(payload.transaction_id))
    if not isinstance(payload, ErrorStatus):
        raise PetProtocolError("Error body type mismatch")
    reason = _reason(payload.reason)
    required = _uint("required_bytes", payload.required_bytes, 0, 0xFFFFFFFF)
    available = _uint("available_bytes", payload.available_bytes, 0, 0xFFFFFFFF)
    if reason in (ErrorReason.INSUFFICIENT_STORAGE, ErrorReason.TOO_LARGE):
        if required <= available:
            raise PetProtocolError(
                "sized pet error must have required_bytes greater than available_bytes"
            )
    elif required != 0 or available != 0:
        raise PetProtocolError("unsized pet error must have zero byte counts")
    return ERROR_BODY.pack(
        _transaction_id(payload.transaction_id),
        int(reason),
        0,
        required,
        available,
    )


def _encode(key: bytes, nonce: bytes, sequence: int, kind: MessageKind, body: bytes, domain: int) -> bytes:
    nonce = _nonce(nonce)
    sequence = _sequence(sequence)
    prefix = PREFIX.pack(int(kind), PROTOCOL_VERSION, sequence, nonce)
    try:
        return prefix + encrypt(key, domain, nonce, sequence, body, prefix)
    except (TypeError, ValueError) as error:
        raise PetProtocolError(str(error)) from None


def encode_control(
    key: bytes, nonce: bytes, sequence: int, control: PetControl
) -> bytes:
    body = _control_body(control)
    kind = _kind(control.kind, CONTROL_KINDS)
    return _encode(key, nonce, sequence, kind, body, PET_CONTROL_DOMAIN)


def encode_status(
    key: bytes, nonce: bytes, sequence: int, status: PetStatus
) -> bytes:
    body = _status_body(status)
    kind = _kind(status.kind, STATUS_KINDS)
    return _encode(key, nonce, sequence, kind, body, PET_STATUS_DOMAIN)


def encode_query(key: bytes, nonce: bytes, sequence: int, transaction_id: int) -> bytes:
    return encode_control(
        key, nonce, sequence, PetControl(MessageKind.QUERY, Query(transaction_id))
    )


def encode_begin(
    key: bytes,
    nonce: bytes,
    sequence: int,
    transaction_id: int,
    schema: int,
    width: int,
    height: int,
    frame_count: int,
    package_length: int,
    sha256: bytes,
) -> bytes:
    return encode_control(
        key,
        nonce,
        sequence,
        PetControl(
            MessageKind.BEGIN,
            Begin(
                transaction_id,
                schema,
                width,
                height,
                frame_count,
                package_length,
                sha256,
            ),
        ),
    )


def encode_chunk(
    key: bytes,
    nonce: bytes,
    sequence: int,
    transaction_id: int,
    offset: int,
    data: bytes,
) -> bytes:
    return encode_control(
        key,
        nonce,
        sequence,
        PetControl(MessageKind.CHUNK, Chunk(transaction_id, offset, data)),
    )


def _encode_transaction(
    key: bytes,
    nonce: bytes,
    sequence: int,
    transaction_id: int,
    kind: MessageKind,
) -> bytes:
    return encode_control(
        key, nonce, sequence, PetControl(kind, Transaction(transaction_id))
    )


def encode_commit(key: bytes, nonce: bytes, sequence: int, transaction_id: int) -> bytes:
    return _encode_transaction(key, nonce, sequence, transaction_id, MessageKind.COMMIT)


def encode_cancel(key: bytes, nonce: bytes, sequence: int, transaction_id: int) -> bytes:
    return _encode_transaction(key, nonce, sequence, transaction_id, MessageKind.CANCEL)


def encode_restore(key: bytes, nonce: bytes, sequence: int, transaction_id: int) -> bytes:
    return _encode_transaction(key, nonce, sequence, transaction_id, MessageKind.RESTORE)


def _decode_prefix(key: bytes, frame: bytes, allowed, domain: int):
    if not isinstance(frame, bytes) or not PREFIX.size + TAG_BYTES + 4 <= len(frame) <= MAX_FRAME_BYTES:
        raise PetProtocolError("invalid pet frame size")
    raw_kind, version, sequence, nonce = PREFIX.unpack_from(frame)
    kind = _kind(raw_kind, allowed)
    if version != PROTOCOL_VERSION:
        raise PetProtocolError("unsupported pet protocol version")
    _sequence(sequence)
    _nonce(nonce)
    try:
        body = decrypt(
            key,
            domain,
            nonce,
            sequence,
            frame[PREFIX.size :],
            frame[: PREFIX.size],
        )
    except (InvalidTag, TypeError, ValueError):
        raise PetProtocolError("pet frame authentication failed") from None
    return kind, sequence, nonce, body


def decode_control(key: bytes, frame: bytes) -> PetControl:
    kind, sequence, nonce, body = _decode_prefix(
        key, frame, CONTROL_KINDS, PET_CONTROL_DOMAIN
    )
    try:
        if kind == MessageKind.QUERY:
            if len(body) != QUERY_BODY.size:
                raise PetProtocolError("invalid Query body size")
            payload: ControlPayload = Query(QUERY_BODY.unpack(body)[0])
        elif kind == MessageKind.BEGIN:
            if len(body) != BEGIN_BODY.size:
                raise PetProtocolError("invalid Begin body size")
            payload = Begin(*BEGIN_BODY.unpack(body))
        elif kind == MessageKind.CHUNK:
            if len(body) < CHUNK_HEADER.size:
                raise PetProtocolError("invalid Chunk body size")
            transaction_id, offset, data_length = CHUNK_HEADER.unpack_from(body)
            if data_length != len(body) - CHUNK_HEADER.size:
                raise PetProtocolError("Chunk data length mismatch")
            payload = Chunk(transaction_id, offset, body[CHUNK_HEADER.size :])
        else:
            if len(body) != QUERY_BODY.size:
                raise PetProtocolError("invalid transaction body size")
            payload = Transaction(QUERY_BODY.unpack(body)[0])
    except struct.error:
        raise PetProtocolError("malformed control body") from None
    decoded = PetControl(kind, payload, sequence, nonce)
    if _control_body(decoded) != body:
        raise PetProtocolError("non-canonical control body")
    return decoded


def decode_status(key: bytes, frame: bytes) -> PetStatus:
    kind, sequence, nonce, body = _decode_prefix(
        key, frame, STATUS_KINDS, PET_STATUS_DOMAIN
    )
    try:
        if kind == MessageKind.STATE:
            if len(body) != STATE_BODY.size:
                raise PetProtocolError("invalid State body size")
            (
                transaction_id,
                source,
                usable,
                schema_min,
                schema_max,
                reserved,
                generation,
                digest,
            ) = STATE_BODY.unpack(body)
            if reserved != 0 or usable not in (0, 1):
                raise PetProtocolError("invalid State reserved or usable field")
            payload: StatusPayload = StateStatus(
                transaction_id,
                _source(source),
                bool(usable),
                schema_min,
                schema_max,
                generation,
                digest,
            )
        elif kind == MessageKind.PROGRESS:
            if len(body) != PROGRESS_BODY.size:
                raise PetProtocolError("invalid Progress body size")
            payload = ProgressStatus(*PROGRESS_BODY.unpack(body))
        elif kind == MessageKind.COMMITTED:
            if len(body) != COMMITTED_BODY.size:
                raise PetProtocolError("invalid Committed body size")
            payload = CommittedStatus(*COMMITTED_BODY.unpack(body))
        elif kind == MessageKind.CANCELLED:
            if len(body) != QUERY_BODY.size:
                raise PetProtocolError("invalid Cancelled body size")
            payload = CancelledStatus(QUERY_BODY.unpack(body)[0])
        else:
            if len(body) != ERROR_BODY.size:
                raise PetProtocolError("invalid Error body size")
            transaction_id, reason, reserved, required, available = ERROR_BODY.unpack(body)
            if reserved != 0:
                raise PetProtocolError("invalid Error reserved field")
            payload = ErrorStatus(
                transaction_id, _reason(reason), required, available
            )
    except struct.error:
        raise PetProtocolError("malformed status body") from None
    decoded = PetStatus(kind, payload, sequence, nonce)
    if _status_body(decoded) != body:
        raise PetProtocolError("non-canonical status body")
    return decoded


class ControlReplayGuard:
    """Owns one operation plus bounded transaction/nonce tombstones."""

    def __init__(self) -> None:
        self._generation: Optional[int] = None
        self._active_nonce: Optional[bytes] = None
        self._transaction_id: Optional[int] = None
        self._last_sequence = 0
        self._next_offset = 0
        self._last_chunk_sequence = 0
        self._last_chunk_offset = 0
        self._last_chunk_frame: Optional[bytes] = None
        self._tombstones: Deque[tuple[int, bytes]] = deque(
            maxlen=COMPLETED_TRANSACTION_TOMBSTONES
        )

    def set_connection_generation(self, generation: int) -> None:
        generation = _uint("connection_generation", generation, 1, 0xFFFFFFFF)
        if generation == self._generation:
            return
        self._generation = generation
        self._active_nonce = None
        self._transaction_id = None
        self._last_sequence = 0
        self._next_offset = 0
        self._last_chunk_frame = None
        self._tombstones.clear()

    def begin(self, nonce: bytes, transaction_id: int) -> bool:
        nonce = _nonce(nonce)
        transaction_id = _transaction_id(transaction_id)
        if self._generation is None or self._active_nonce is not None:
            return False
        if any(
            old_transaction == transaction_id or old_nonce == nonce
            for old_transaction, old_nonce in self._tombstones
        ):
            return False
        self._active_nonce = nonce
        self._transaction_id = transaction_id
        self._last_sequence = 0
        self._next_offset = 0
        self._last_chunk_sequence = 0
        self._last_chunk_offset = 0
        self._last_chunk_frame = None
        return True

    def accept(self, control: PetControl, frame: bytes) -> ReplayDecision:
        if not isinstance(control, PetControl):
            raise PetProtocolError("control must be PetControl")
        if not isinstance(frame, bytes) or not 1 <= len(frame) <= MAX_FRAME_BYTES:
            raise PetProtocolError("frame must be bounded bytes")
        if (
            self._active_nonce is None
            or control.exchange_nonce != self._active_nonce
            or control.transaction_id != self._transaction_id
        ):
            return ReplayDecision.REJECTED
        _control_body(control)
        sequence = _sequence(control.sequence)
        if self._last_sequence == 0 and sequence != 1:
            return ReplayDecision.REJECTED
        if sequence == self._last_sequence:
            if (
                control.kind == MessageKind.CHUNK
                and sequence == self._last_chunk_sequence
                and control.chunk.offset == self._last_chunk_offset
                and frame == self._last_chunk_frame
            ):
                return ReplayDecision.DUPLICATE
            return ReplayDecision.REJECTED
        if sequence < self._last_sequence:
            return ReplayDecision.REJECTED
        if control.kind == MessageKind.CHUNK:
            chunk = control.chunk
            if chunk.offset != self._next_offset:
                return ReplayDecision.REJECTED
            self._next_offset += len(chunk.data)
            self._last_chunk_sequence = sequence
            self._last_chunk_offset = chunk.offset
            self._last_chunk_frame = frame
        else:
            self._last_chunk_sequence = 0
            self._last_chunk_frame = None
        self._last_sequence = sequence
        return ReplayDecision.ACCEPTED

    def retire(self, completed: bool) -> None:
        if (
            completed
            and self._active_nonce is not None
            and self._transaction_id is not None
        ):
            self._tombstones.append((self._transaction_id, self._active_nonce))
        self._active_nonce = None
        self._transaction_id = None
        self._last_sequence = 0
        self._next_offset = 0
        self._last_chunk_sequence = 0
        self._last_chunk_frame = None


class StatusReplayGuard:
    """Accepts strictly increasing status sequences for one active nonce."""

    def __init__(self) -> None:
        self._active_nonce: Optional[bytes] = None
        self._last_sequence = 0

    def set_active_nonce(self, nonce: bytes) -> None:
        nonce = _nonce(nonce)
        if self._active_nonce is not None:
            raise PetProtocolError("status replay guard is already active")
        self._active_nonce = nonce
        self._last_sequence = 0

    def accept(self, status: PetStatus) -> bool:
        if not isinstance(status, PetStatus):
            raise PetProtocolError("status must be PetStatus")
        if self._active_nonce is None or status.exchange_nonce != self._active_nonce:
            return False
        _status_body(status)
        sequence = _sequence(status.sequence)
        if self._last_sequence == 0 and sequence != 1:
            return False
        if sequence <= self._last_sequence:
            return False
        self._last_sequence = sequence
        return True

    def retire(self) -> None:
        self._active_nonce = None
        self._last_sequence = 0
