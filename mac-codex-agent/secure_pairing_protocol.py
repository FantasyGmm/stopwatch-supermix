"""Canonical wire encoding for StopWatch secure Agent pairing."""

from dataclasses import dataclass
import struct
import unicodedata

from cryptography.hazmat.primitives.asymmetric import ec


PAIRING_VERSION = 1
_MAGIC = b"SWPAIR"
_ID_BYTES = 16
_NONCE_BYTES = 16
_PUBLIC_KEY_BYTES = 33
_MAX_AGENT_NAME_BYTES = 24


@dataclass(frozen=True)
class PairingTranscript:
    transaction_id: int
    watch_id: bytes
    watch_nonce: bytes
    agent_nonce: bytes
    watch_ephemeral_public: bytes
    watch_identity_public: bytes
    agent_ephemeral_public: bytes
    agent_identity_public: bytes
    agent_name: str


def _encode_field(value: bytes) -> bytes:
    return struct.pack(">H", len(value)) + value


def _validate_public_key(value: bytes) -> None:
    if len(value) != _PUBLIC_KEY_BYTES or value[0] not in (2, 3):
        raise ValueError("public key must be a compressed P-256 point")
    try:
        ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), value)
    except ValueError as error:
        raise ValueError("public key is not on P-256") from error


def _encode_name(value: str) -> bytes:
    if not value or unicodedata.normalize("NFC", value) != value:
        raise ValueError("Agent name must be nonempty NFC text")
    if any(unicodedata.category(char).startswith("C") for char in value):
        raise ValueError("Agent name cannot contain control characters")
    encoded = value.encode("utf-8")
    if len(encoded) > _MAX_AGENT_NAME_BYTES:
        raise ValueError("Agent name exceeds 24 UTF-8 bytes")
    return encoded


def _validate_transcript(value: PairingTranscript) -> bytes:
    if not 0 <= value.transaction_id <= 0xFFFFFFFF:
        raise ValueError("transaction identifier is out of range")
    if len(value.watch_id) != _ID_BYTES:
        raise ValueError("watch identifier must be 16 bytes")
    if len(value.watch_nonce) != _NONCE_BYTES or not any(value.watch_nonce):
        raise ValueError("watch nonce must be 16 nonzero bytes")
    if len(value.agent_nonce) != _NONCE_BYTES or not any(value.agent_nonce):
        raise ValueError("Agent nonce must be 16 nonzero bytes")
    _validate_public_key(value.watch_ephemeral_public)
    _validate_public_key(value.watch_identity_public)
    _validate_public_key(value.agent_ephemeral_public)
    _validate_public_key(value.agent_identity_public)
    return _encode_name(value.agent_name)


def encode_pairing_transcript(value: PairingTranscript) -> bytes:
    name = _validate_transcript(value)
    fields = (
        value.watch_id,
        value.watch_nonce,
        value.agent_nonce,
        value.watch_ephemeral_public,
        value.watch_identity_public,
        value.agent_ephemeral_public,
        value.agent_identity_public,
        name,
    )
    return (
        _MAGIC
        + bytes((PAIRING_VERSION,))
        + struct.pack(">I", value.transaction_id)
        + b"".join(_encode_field(field) for field in fields)
    )


def decode_pairing_transcript(data: bytes) -> PairingTranscript:
    if len(data) < len(_MAGIC) + 5 or not data.startswith(_MAGIC):
        raise ValueError("invalid pairing transcript magic")
    offset = len(_MAGIC)
    version = data[offset]
    offset += 1
    if version != PAIRING_VERSION:
        raise ValueError("unsupported pairing protocol version")
    transaction_id = struct.unpack_from(">I", data, offset)[0]
    offset += 4

    fields = []
    for _ in range(8):
        if offset + 2 > len(data):
            raise ValueError("truncated pairing transcript")
        size = struct.unpack_from(">H", data, offset)[0]
        offset += 2
        if offset + size > len(data):
            raise ValueError("truncated pairing field")
        fields.append(data[offset : offset + size])
        offset += size
    if offset != len(data):
        raise ValueError("trailing pairing transcript bytes")
    try:
        name = fields[7].decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError("Agent name is not UTF-8") from error

    result = PairingTranscript(
        transaction_id=transaction_id,
        watch_id=fields[0],
        watch_nonce=fields[1],
        agent_nonce=fields[2],
        watch_ephemeral_public=fields[3],
        watch_identity_public=fields[4],
        agent_ephemeral_public=fields[5],
        agent_identity_public=fields[6],
        agent_name=name,
    )
    _validate_transcript(result)
    if encode_pairing_transcript(result) != data:
        raise ValueError("pairing transcript is not canonical")
    return result
