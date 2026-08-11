"""Fixed-size authenticated framing for StopWatch input actions."""

import dataclasses
import enum
import struct
from typing import Optional

from voice_input_crypto import (
    INPUT_FORCE_STEER_DOMAIN,
    INPUT_READY_DOMAIN,
    INPUT_SUBMIT_DOMAIN,
    TAG_BYTES,
    decrypt,
    encrypt,
)


PROTOCOL_VERSION = 1
READY = 1
CONTROL_AAD = struct.Struct("<BBH8s")
ACTION_HEADER = struct.Struct("<BBIIH")


class InputAction(enum.IntEnum):
    SUBMIT = 1
    FORCE_STEER = 2


@dataclasses.dataclass(frozen=True)
class ActionPacket:
    action: InputAction
    sequence: int
    watch_ms: int


_ACTION_DOMAINS = {
    InputAction.SUBMIT: INPUT_SUBMIT_DOMAIN,
    InputAction.FORCE_STEER: INPUT_FORCE_STEER_DOMAIN,
}


def _validate_nonce(connection_nonce: bytes) -> None:
    if (
        not isinstance(connection_nonce, bytes)
        or len(connection_nonce) != 8
        or not any(connection_nonce)
    ):
        raise ValueError("connection nonce must be 8 nonzero bytes")


def _validate_action_values(
    action: InputAction,
    sequence: int,
    watch_ms: int,
    connection_nonce: bytes,
) -> InputAction:
    try:
        valid_action = InputAction(action)
    except (TypeError, ValueError):
        raise ValueError("unknown input action") from None
    if (
        isinstance(sequence, bool)
        or not isinstance(sequence, int)
        or not 1 <= sequence <= 0xFFFFFFFF
    ):
        raise ValueError("sequence must be 1..4294967295")
    if (
        isinstance(watch_ms, bool)
        or not isinstance(watch_ms, int)
        or not 0 <= watch_ms <= 0xFFFFFFFF
    ):
        raise ValueError("watch_ms must be 0..4294967295")
    _validate_nonce(connection_nonce)
    return valid_action


def encode_ready(key: bytes, connection_nonce: bytes) -> bytes:
    _validate_nonce(connection_nonce)
    aad = CONTROL_AAD.pack(PROTOCOL_VERSION, READY, 0, connection_nonce)
    return aad + encrypt(
        key, INPUT_READY_DOMAIN, connection_nonce, 0, b"", aad
    )


def decode_ready(key: bytes, raw: bytes) -> bytes:
    if len(raw) != CONTROL_AAD.size + TAG_BYTES:
        raise ValueError("invalid input Ready size")
    version, kind, reserved, connection_nonce = CONTROL_AAD.unpack_from(raw)
    if version != PROTOCOL_VERSION or kind != READY or reserved != 0:
        raise ValueError("invalid input Ready header")
    _validate_nonce(connection_nonce)
    if decrypt(
        key,
        INPUT_READY_DOMAIN,
        connection_nonce,
        0,
        raw[CONTROL_AAD.size :],
        raw[: CONTROL_AAD.size],
    ) != b"":
        raise ValueError("input Ready body must be empty")
    return connection_nonce


def encode_action(
    key: bytes,
    action: InputAction,
    sequence: int,
    watch_ms: int,
    connection_nonce: bytes,
) -> bytes:
    action = _validate_action_values(
        action, sequence, watch_ms, connection_nonce
    )
    header = ACTION_HEADER.pack(
        PROTOCOL_VERSION, int(action), sequence, watch_ms, TAG_BYTES
    )
    return header + encrypt(
        key,
        _ACTION_DOMAINS[action],
        connection_nonce,
        sequence,
        b"",
        header,
    )


def decode_action(
    key: bytes, raw: bytes, connection_nonce: bytes
) -> ActionPacket:
    if len(raw) != ACTION_HEADER.size + TAG_BYTES:
        raise ValueError("invalid input action size")
    version, raw_action, sequence, watch_ms, body_size = ACTION_HEADER.unpack_from(
        raw
    )
    try:
        action = InputAction(raw_action)
    except ValueError:
        raise ValueError("unknown input action") from None
    if version != PROTOCOL_VERSION or body_size != TAG_BYTES:
        raise ValueError("invalid input action header")
    action = _validate_action_values(
        action, sequence, watch_ms, connection_nonce
    )
    if decrypt(
        key,
        _ACTION_DOMAINS[action],
        connection_nonce,
        sequence,
        raw[ACTION_HEADER.size :],
        raw[: ACTION_HEADER.size],
    ) != b"":
        raise ValueError("input action body must be empty")
    return ActionPacket(action, sequence, watch_ms)


class ActionReplayGuard:
    """Accepts increasing sequences only for an explicitly active nonce."""

    def __init__(self) -> None:
        self._active_nonce: Optional[bytes] = None
        self._last_sequence = 0

    def set_active_nonce(self, connection_nonce: bytes) -> None:
        _validate_nonce(connection_nonce)
        if connection_nonce != self._active_nonce:
            self._active_nonce = connection_nonce
            self._last_sequence = 0

    def accept(self, connection_nonce: bytes, sequence: int) -> bool:
        if isinstance(sequence, bool) or not isinstance(sequence, int):
            raise ValueError("sequence must be a non-boolean integer")
        if (
            connection_nonce != self._active_nonce
            or not 1 <= sequence <= 0xFFFFFFFF
            or sequence <= self._last_sequence
        ):
            return False
        self._last_sequence = sequence
        return True
