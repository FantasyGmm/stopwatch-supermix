"""Authenticated version-4 framing for production StopWatch voice input."""

import dataclasses
import struct
from typing import Optional

from voice_input_crypto import (
    AUDIO_DOMAIN,
    PTT_BEGIN_DOMAIN,
    PTT_END_DOMAIN,
    READY_DOMAIN,
    TAG_BYTES,
    UNAVAILABLE_DOMAIN,
    VOICE_ERROR_DOMAIN,
    decrypt,
    derive_nonce,
    encrypt,
    validate_key_epoch,
)


PROTOCOL_VERSION = 4
READY = 1
UNAVAILABLE = 2
PTT_BEGIN = 1
AUDIO = 2
PTT_END = 3
VOICE_ERROR = 4

IMA_ADPCM = 1
SAMPLE_RATE = 16000
BLOCK_SAMPLES = 320
AUDIO_BYTES = 164

DRIVER_REQUIRED = 1
RESTART_REQUIRED = 2
ACCESSIBILITY_REQUIRED = 3
TYPELESS_NOT_RUNNING = 4
INITIALIZATION_FAILED = 5
UNAVAILABLE_REASONS = frozenset(
    {
        DRIVER_REQUIRED,
        RESTART_REQUIRED,
        ACCESSIBILITY_REQUIRED,
        TYPELESS_NOT_RUNNING,
        INITIALIZATION_FAILED,
    }
)

CONTROL_AAD = struct.Struct("<BBBBI8s")
CONTROL_BODY = struct.Struct("<I")
HEADER = struct.Struct("<BBHIIIH")
BEGIN_BODY = struct.Struct("<BHH")
END_PAYLOAD = struct.Struct("<IIIIIIH")
ERROR_PAYLOAD = struct.Struct("<HH")

_CONTROL_DOMAINS = {READY: READY_DOMAIN, UNAVAILABLE: UNAVAILABLE_DOMAIN}
_NOTIFICATION_DOMAINS = {
    PTT_BEGIN: PTT_BEGIN_DOMAIN,
    AUDIO: AUDIO_DOMAIN,
    PTT_END: PTT_END_DOMAIN,
    VOICE_ERROR: VOICE_ERROR_DOMAIN,
}
_PAYLOAD_SIZES = {
    PTT_BEGIN: BEGIN_BODY.size,
    AUDIO: AUDIO_BYTES,
    PTT_END: END_PAYLOAD.size,
    VOICE_ERROR: ERROR_PAYLOAD.size,
}


@dataclasses.dataclass(frozen=True)
class Control:
    kind: int
    reason: int
    key_epoch: int
    exchange_nonce: bytes
    target_bps: int


@dataclasses.dataclass(frozen=True)
class Notification:
    kind: int
    session: int
    sequence: int
    watch_ms: int
    key_epoch: int
    payload: bytes
    session_nonce: Optional[bytes] = None


@dataclasses.dataclass(frozen=True)
class EndSummary:
    generated_packets: int
    accepted_packets: int
    queue_overflows: int
    encode_failures: int
    captured_samples: int
    elapsed_ms: int
    negotiated_mtu: int

    def pack(self) -> bytes:
        return END_PAYLOAD.pack(
            self.generated_packets,
            self.accepted_packets,
            self.queue_overflows,
            self.encode_failures,
            self.captured_samples,
            self.elapsed_ms,
            self.negotiated_mtu,
        )

    @classmethod
    def from_payload(cls, payload: bytes) -> "EndSummary":
        if len(payload) != END_PAYLOAD.size:
            raise ValueError("invalid PTT_END body size")
        return cls(*END_PAYLOAD.unpack(payload))


def encode_ready(
    key: bytes,
    exchange_nonce: bytes,
    target_bps: int,
    *,
    key_epoch: int,
) -> bytes:
    if target_bps != 8192:
        raise ValueError("READY target bitrate must be 8192")
    return _encode_control(
        key, READY, 0, key_epoch, exchange_nonce, target_bps
    )


def encode_unavailable(
    key: bytes,
    exchange_nonce: bytes,
    reason: int,
    *,
    key_epoch: int,
) -> bytes:
    if reason not in UNAVAILABLE_REASONS:
        raise ValueError("UNAVAILABLE reason is not defined")
    return _encode_control(
        key, UNAVAILABLE, reason, key_epoch, exchange_nonce, 0
    )


def _encode_control(
    key: bytes,
    kind: int,
    reason: int,
    key_epoch: int,
    exchange_nonce: bytes,
    target_bps: int,
) -> bytes:
    validate_key_epoch(key_epoch)
    domain = _CONTROL_DOMAINS[kind]
    derive_nonce(domain, exchange_nonce, 0)
    aad = CONTROL_AAD.pack(
        PROTOCOL_VERSION, kind, reason, 0, key_epoch, exchange_nonce
    )
    body = CONTROL_BODY.pack(target_bps)
    return aad + encrypt(key, domain, exchange_nonce, 0, body, aad)


def decode_control(
    key: bytes,
    raw: bytes,
    *,
    expected_epoch: int,
) -> Control:
    validate_key_epoch(expected_epoch)
    expected_size = CONTROL_AAD.size + CONTROL_BODY.size + TAG_BYTES
    if len(raw) != expected_size:
        raise ValueError("invalid control size")
    (
        version,
        kind,
        reason,
        reserved,
        key_epoch,
        exchange_nonce,
    ) = CONTROL_AAD.unpack_from(raw)
    if version != PROTOCOL_VERSION:
        raise ValueError("unsupported production protocol version")
    if kind not in _CONTROL_DOMAINS:
        raise ValueError("unknown control kind")
    if reserved != 0:
        raise ValueError("control reserved byte must be zero")
    if key_epoch != expected_epoch:
        raise ValueError("voice key epoch does not match")

    aad = raw[: CONTROL_AAD.size]
    body = decrypt(
        key,
        _CONTROL_DOMAINS[kind],
        exchange_nonce,
        0,
        raw[CONTROL_AAD.size :],
        aad,
    )
    target_bps = CONTROL_BODY.unpack(body)[0]
    if kind == READY and (reason != 0 or target_bps != 8192):
        raise ValueError("invalid READY control")
    if kind == UNAVAILABLE and (
        reason not in UNAVAILABLE_REASONS or target_bps != 0
    ):
        raise ValueError("invalid UNAVAILABLE control")
    return Control(kind, reason, key_epoch, exchange_nonce, target_bps)


def _validate_header_values(session: int, sequence: int, watch_ms: int) -> None:
    if not 1 <= session <= 0xFFFF:
        raise ValueError("session must be 1..65535")
    if not 0 <= sequence <= 0xFFFFFFFF:
        raise ValueError("sequence must be 0..4294967295")
    if not 0 <= watch_ms <= 0xFFFFFFFF:
        raise ValueError("watch_ms must be 0..4294967295")


def _validate_begin(sequence: int, payload: bytes) -> None:
    if sequence != 0:
        raise ValueError("PTT_BEGIN sequence must be zero")
    if len(payload) != BEGIN_BODY.size:
        raise ValueError("invalid PTT_BEGIN body size")
    if BEGIN_BODY.unpack(payload) != (IMA_ADPCM, SAMPLE_RATE, BLOCK_SAMPLES):
        raise ValueError("invalid production codec tuple")


def _validate_notification_payload(
    kind: int, sequence: int, payload: bytes
) -> None:
    if len(payload) != _PAYLOAD_SIZES[kind]:
        raise ValueError("invalid logical notification payload size")
    if kind == PTT_BEGIN:
        _validate_begin(sequence, payload)
    elif sequence == 0:
        raise ValueError("session notification sequence must be nonzero")
    if kind == PTT_END:
        EndSummary.from_payload(payload)
    elif kind == VOICE_ERROR:
        ERROR_PAYLOAD.unpack(payload)


def pack_notification(
    key: bytes,
    kind: int,
    session: int,
    sequence: int,
    watch_ms: int,
    payload: bytes,
    session_nonce: bytes,
    *,
    key_epoch: int,
) -> bytes:
    validate_key_epoch(key_epoch)
    _validate_header_values(session, sequence, watch_ms)
    if kind not in _NOTIFICATION_DOMAINS:
        raise ValueError("unknown notification kind")
    _validate_notification_payload(kind, sequence, payload)

    clear_nonce_size = 8 if kind == PTT_BEGIN else 0
    header = HEADER.pack(
        PROTOCOL_VERSION,
        kind,
        session,
        sequence,
        watch_ms,
        key_epoch,
        clear_nonce_size + len(payload) + TAG_BYTES,
    )
    aad = header + session_nonce if kind == PTT_BEGIN else header
    ciphertext = encrypt(
        key,
        _NOTIFICATION_DOMAINS[kind],
        session_nonce,
        sequence,
        payload,
        aad,
    )
    return aad + ciphertext if kind == PTT_BEGIN else header + ciphertext


def decode_notification(
    key: bytes,
    raw: bytes,
    session_nonce: Optional[bytes] = None,
    *,
    expected_epoch: int,
) -> Notification:
    validate_key_epoch(expected_epoch)
    if len(raw) < HEADER.size:
        raise ValueError("notification is shorter than its header")
    (
        version,
        kind,
        session,
        sequence,
        watch_ms,
        key_epoch,
        payload_length,
    ) = HEADER.unpack_from(raw)
    if version != PROTOCOL_VERSION:
        raise ValueError("unsupported production protocol version")
    if key_epoch != expected_epoch:
        raise ValueError("voice key epoch does not match")
    _validate_header_values(session, sequence, watch_ms)
    if kind not in _NOTIFICATION_DOMAINS:
        raise ValueError("unknown notification kind")
    if len(raw) != HEADER.size + payload_length:
        raise ValueError("invalid notification payload size")
    clear_nonce_size = 8 if kind == PTT_BEGIN else 0
    expected_payload_length = clear_nonce_size + _PAYLOAD_SIZES[kind] + TAG_BYTES
    if payload_length != expected_payload_length:
        raise ValueError("invalid encrypted notification payload size")
    if kind == PTT_BEGIN:
        if sequence != 0:
            raise ValueError("PTT_BEGIN sequence must be zero")
        packet_nonce = raw[HEADER.size : HEADER.size + 8]
        aad = raw[: HEADER.size + 8]
        ciphertext = raw[HEADER.size + 8 :]
    else:
        if sequence == 0:
            raise ValueError("session notification sequence must be nonzero")
        if session_nonce is None:
            raise ValueError("session nonce is required for this notification")
        packet_nonce = session_nonce
        aad = raw[:HEADER.size]
        ciphertext = raw[HEADER.size:]
    payload = decrypt(
        key,
        _NOTIFICATION_DOMAINS[kind],
        packet_nonce,
        sequence,
        ciphertext,
        aad,
    )
    _validate_notification_payload(kind, sequence, payload)
    return Notification(
        kind,
        session,
        sequence,
        watch_ms,
        key_epoch,
        payload,
        packet_nonce,
    )
