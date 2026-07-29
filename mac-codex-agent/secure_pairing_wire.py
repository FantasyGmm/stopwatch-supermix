"""Bounded binary frames for the restricted secure-pairing GATT surface."""

from __future__ import annotations

from enum import IntEnum
import struct

from pairing_client import (
    AuthenticationChallenge,
    AuthenticationRequest,
    BeginPairingRequest,
    CredentialEnvelope,
    CredentialPurpose,
    PairingOffer,
)


WIRE_MAGIC = b"SWPR"
WIRE_VERSION = 1
MAX_WIRE_FRAME_SIZE = 256
MAX_AGENT_NAME_BYTES = 24

_HEADER = struct.Struct(">4sBBIH")
_DISCOVERY_RESPONSE = struct.Struct(">16sI")
_PAIRING_REQUEST = struct.Struct(">16s33s33sB")
_PAIRING_OFFER = struct.Struct(">16s16s33s")
_AUTHENTICATION_REQUEST = struct.Struct(">16sI")
_AUTHENTICATION_CHALLENGE = struct.Struct(">16s16sIB")
_CREDENTIAL_ENVELOPE = struct.Struct(">BI33s13s13sH")
_LENGTH_PREFIX = struct.Struct(">H")


class PairingWireError(ValueError):
    """A malformed, unexpected, or oversized pairing frame."""


class WireMessageType(IntEnum):
    DISCOVERY_REQUEST = 1
    DISCOVERY_RESPONSE = 2
    PAIRING_REQUEST = 3
    PAIRING_OFFER = 4
    PAIRING_CONFIRM = 5
    CREDENTIAL = 6
    CREDENTIAL_ACK = 7
    AUTHENTICATION_REQUEST = 8
    AUTHENTICATION_CHALLENGE = 9
    AUTHENTICATION_PROOF = 10
    AUTHENTICATION_OK = 11
    REJECTED = 12
    CREDENTIAL_ACK_OK = 13
    ERROR = 14


def _transaction(value: int) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value <= 0
        or value > 0xFFFFFFFF
    ):
        raise PairingWireError("pairing transaction is invalid")
    return value


def _bytes(value: bytes, size: int, label: str, *, nonzero=True) -> bytes:
    if not isinstance(value, bytes) or len(value) != size:
        raise PairingWireError("{} has invalid size".format(label))
    if nonzero and not any(value):
        raise PairingWireError("{} cannot be zero".format(label))
    return value


def encode_frame(kind: WireMessageType, transaction_id: int, payload: bytes) -> bytes:
    try:
        kind = WireMessageType(kind)
    except ValueError:
        raise PairingWireError("pairing frame type is invalid") from None
    _transaction(transaction_id)
    if not isinstance(payload, bytes):
        raise PairingWireError("pairing payload must be bytes")
    if _HEADER.size + len(payload) > MAX_WIRE_FRAME_SIZE:
        raise PairingWireError("pairing frame is too large")
    return _HEADER.pack(
        WIRE_MAGIC,
        WIRE_VERSION,
        int(kind),
        transaction_id,
        len(payload),
    ) + payload


def decode_frame(frame: bytes):
    if not isinstance(frame, bytes) or len(frame) < _HEADER.size:
        raise PairingWireError("pairing frame is truncated")
    if len(frame) > MAX_WIRE_FRAME_SIZE:
        raise PairingWireError("pairing frame is too large")
    magic, version, raw_kind, transaction_id, payload_size = _HEADER.unpack_from(
        frame
    )
    try:
        kind = WireMessageType(raw_kind)
    except ValueError:
        raise PairingWireError("pairing frame type is invalid") from None
    if magic != WIRE_MAGIC or version != WIRE_VERSION:
        raise PairingWireError("pairing frame version is invalid")
    _transaction(transaction_id)
    payload = frame[_HEADER.size :]
    if payload_size != len(payload):
        raise PairingWireError("pairing frame length is invalid")
    return kind, transaction_id, payload


def encode_discovery_request(transaction_id: int) -> bytes:
    return encode_frame(WireMessageType.DISCOVERY_REQUEST, transaction_id, b"")


def encode_discovery_response(
    transaction_id: int, watch_id: bytes, key_epoch: int
) -> bytes:
    _bytes(watch_id, 16, "watch identifier")
    if key_epoch <= 0 or key_epoch > 0xFFFFFFFF:
        raise PairingWireError("watch key epoch is invalid")
    return encode_frame(
        WireMessageType.DISCOVERY_RESPONSE,
        transaction_id,
        _DISCOVERY_RESPONSE.pack(watch_id, key_epoch),
    )


def decode_discovery_response(payload: bytes):
    if len(payload) != _DISCOVERY_RESPONSE.size:
        raise PairingWireError("discovery response has invalid size")
    watch_id, key_epoch = _DISCOVERY_RESPONSE.unpack(payload)
    _bytes(watch_id, 16, "watch identifier")
    if key_epoch <= 0:
        raise PairingWireError("watch key epoch is invalid")
    return watch_id, key_epoch


def encode_pairing_request(request: BeginPairingRequest) -> bytes:
    name = request.display_name.encode("utf-8")
    if not name or len(name) > MAX_AGENT_NAME_BYTES:
        raise PairingWireError("Agent name has invalid size")
    payload = _PAIRING_REQUEST.pack(
        _bytes(request.agent_nonce, 16, "Agent nonce"),
        _bytes(request.agent_ephemeral_public, 33, "Agent ephemeral key"),
        _bytes(request.agent_identity_public, 33, "Agent identity key"),
        len(name),
    ) + name
    return encode_frame(WireMessageType.PAIRING_REQUEST, request.transaction_id, payload)


def decode_pairing_request(
    transaction_id: int, payload: bytes
) -> BeginPairingRequest:
    _transaction(transaction_id)
    if len(payload) < _PAIRING_REQUEST.size:
        raise PairingWireError("pairing request is truncated")
    nonce, ephemeral, identity, name_size = _PAIRING_REQUEST.unpack_from(payload)
    name = payload[_PAIRING_REQUEST.size :]
    if name_size != len(name) or not name or name_size > MAX_AGENT_NAME_BYTES:
        raise PairingWireError("Agent name has invalid size")
    try:
        display_name = name.decode("utf-8")
    except UnicodeDecodeError:
        raise PairingWireError("Agent name is not UTF-8") from None
    return BeginPairingRequest(
        transaction_id,
        _bytes(nonce, 16, "Agent nonce"),
        _bytes(ephemeral, 33, "Agent ephemeral key"),
        _bytes(identity, 33, "Agent identity key"),
        display_name,
    )


def encode_pairing_offer(offer: PairingOffer) -> bytes:
    return encode_frame(
        WireMessageType.PAIRING_OFFER,
        offer.transaction_id,
        _PAIRING_OFFER.pack(
            _bytes(offer.watch_id, 16, "watch identifier"),
            _bytes(offer.watch_nonce, 16, "watch nonce"),
            _bytes(offer.watch_ephemeral_public, 33, "watch ephemeral key"),
        ),
    )


def decode_pairing_offer(transaction_id: int, payload: bytes) -> PairingOffer:
    if len(payload) != _PAIRING_OFFER.size:
        raise PairingWireError("pairing offer has invalid size")
    watch_id, nonce, public_key = _PAIRING_OFFER.unpack(payload)
    return PairingOffer(
        _transaction(transaction_id),
        _bytes(watch_id, 16, "watch identifier"),
        _bytes(nonce, 16, "watch nonce"),
        _bytes(public_key, 33, "watch ephemeral key"),
    )


def encode_pairing_confirmation(transaction_id: int, accepted: bool) -> bytes:
    return encode_frame(
        WireMessageType.PAIRING_CONFIRM,
        transaction_id,
        bytes((1 if accepted else 0,)),
    )


def decode_pairing_confirmation(payload: bytes) -> bool:
    if payload not in (b"\x00", b"\x01"):
        raise PairingWireError("pairing confirmation is invalid")
    return payload == b"\x01"


def encode_authentication_request(request: AuthenticationRequest) -> bytes:
    if request.agent_key_epoch <= 0 or request.agent_key_epoch > 0xFFFFFFFF:
        raise PairingWireError("Agent key epoch is invalid")
    return encode_frame(
        WireMessageType.AUTHENTICATION_REQUEST,
        request.transaction_id,
        _AUTHENTICATION_REQUEST.pack(
            _bytes(request.fingerprint, 16, "Agent fingerprint"),
            request.agent_key_epoch,
        ),
    )


def decode_authentication_request(
    transaction_id: int, payload: bytes
) -> AuthenticationRequest:
    if len(payload) != _AUTHENTICATION_REQUEST.size:
        raise PairingWireError("authentication request has invalid size")
    fingerprint, key_epoch = _AUTHENTICATION_REQUEST.unpack(payload)
    if key_epoch <= 0:
        raise PairingWireError("Agent key epoch is invalid")
    return AuthenticationRequest(
        _transaction(transaction_id),
        _bytes(fingerprint, 16, "Agent fingerprint"),
        key_epoch,
    )


def encode_authentication_challenge(challenge: AuthenticationChallenge) -> bytes:
    if challenge.key_epoch <= 0 or challenge.connection_role != 1:
        raise PairingWireError("authentication challenge is invalid")
    return encode_frame(
        WireMessageType.AUTHENTICATION_CHALLENGE,
        challenge.transaction_id,
        _AUTHENTICATION_CHALLENGE.pack(
            _bytes(challenge.watch_id, 16, "watch identifier"),
            _bytes(challenge.connection_nonce, 16, "connection nonce"),
            challenge.key_epoch,
            challenge.connection_role,
        ),
    )


def decode_authentication_challenge(
    transaction_id: int, payload: bytes
) -> AuthenticationChallenge:
    if len(payload) != _AUTHENTICATION_CHALLENGE.size:
        raise PairingWireError("authentication challenge has invalid size")
    watch_id, nonce, key_epoch, role = _AUTHENTICATION_CHALLENGE.unpack(payload)
    if key_epoch <= 0 or role != 1:
        raise PairingWireError("authentication challenge is invalid")
    return AuthenticationChallenge(
        _transaction(transaction_id),
        _bytes(watch_id, 16, "watch identifier"),
        _bytes(nonce, 16, "connection nonce"),
        key_epoch,
        role,
    )


def encode_authentication_proof(transaction_id: int, signature: bytes) -> bytes:
    if not isinstance(signature, bytes) or not signature or len(signature) > 80:
        raise PairingWireError("authentication signature has invalid size")
    return encode_frame(
        WireMessageType.AUTHENTICATION_PROOF,
        transaction_id,
        _LENGTH_PREFIX.pack(len(signature)) + signature,
    )


def decode_authentication_proof(payload: bytes) -> bytes:
    if len(payload) < _LENGTH_PREFIX.size:
        raise PairingWireError("authentication proof is truncated")
    size = _LENGTH_PREFIX.unpack_from(payload)[0]
    signature = payload[_LENGTH_PREFIX.size :]
    if size != len(signature) or not signature or size > 80:
        raise PairingWireError("authentication signature has invalid size")
    return signature


def encode_credential_envelope(envelope: CredentialEnvelope) -> bytes:
    try:
        purpose = CredentialPurpose(envelope.purpose)
    except ValueError:
        raise PairingWireError("credential purpose is invalid") from None
    if envelope.key_epoch <= 0 or envelope.key_epoch > 0xFFFFFFFF:
        raise PairingWireError("credential epoch is invalid")
    if not isinstance(envelope.ciphertext, bytes) or not envelope.ciphertext:
        raise PairingWireError("credential ciphertext is invalid")
    payload = _CREDENTIAL_ENVELOPE.pack(
        int(purpose),
        envelope.key_epoch,
        _bytes(envelope.watch_ephemeral_public, 33, "watch ephemeral key"),
        _bytes(envelope.nonce, 13, "credential nonce"),
        _bytes(envelope.ack_nonce, 13, "acknowledgement nonce"),
        len(envelope.ciphertext),
    ) + envelope.ciphertext
    return encode_frame(WireMessageType.CREDENTIAL, envelope.transaction_id, payload)


def decode_credential_envelope(
    transaction_id: int, payload: bytes
) -> CredentialEnvelope:
    if len(payload) < _CREDENTIAL_ENVELOPE.size:
        raise PairingWireError("credential envelope is truncated")
    purpose, epoch, public_key, nonce, ack_nonce, ciphertext_size = (
        _CREDENTIAL_ENVELOPE.unpack_from(payload)
    )
    ciphertext = payload[_CREDENTIAL_ENVELOPE.size :]
    try:
        purpose_value = CredentialPurpose(purpose)
    except ValueError:
        raise PairingWireError("credential purpose is invalid") from None
    if epoch <= 0 or ciphertext_size != len(ciphertext) or not ciphertext:
        raise PairingWireError("credential envelope is invalid")
    return CredentialEnvelope(
        purpose_value,
        _transaction(transaction_id),
        epoch,
        _bytes(public_key, 33, "watch ephemeral key"),
        _bytes(nonce, 13, "credential nonce"),
        ciphertext,
        _bytes(ack_nonce, 13, "acknowledgement nonce"),
    )


def encode_credential_ack(transaction_id: int, proof: bytes) -> bytes:
    if not isinstance(proof, bytes) or not proof or len(proof) > 32:
        raise PairingWireError("credential acknowledgement is invalid")
    return encode_frame(
        WireMessageType.CREDENTIAL_ACK,
        transaction_id,
        _LENGTH_PREFIX.pack(len(proof)) + proof,
    )


def decode_credential_ack(payload: bytes) -> bytes:
    if len(payload) < _LENGTH_PREFIX.size:
        raise PairingWireError("credential acknowledgement is truncated")
    size = _LENGTH_PREFIX.unpack_from(payload)[0]
    proof = payload[_LENGTH_PREFIX.size :]
    if size != len(proof) or not proof or size > 32:
        raise PairingWireError("credential acknowledgement is invalid")
    return proof


def encode_empty(kind: WireMessageType, transaction_id: int) -> bytes:
    if kind not in (
        WireMessageType.AUTHENTICATION_OK,
        WireMessageType.REJECTED,
        WireMessageType.CREDENTIAL_ACK_OK,
    ):
        raise PairingWireError("empty pairing response type is invalid")
    return encode_frame(kind, transaction_id, b"")
