"""P-256 and AEAD primitives for StopWatch secure Agent pairing."""

from dataclasses import dataclass
import hashlib
import hmac

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.ciphers.aead import AESCCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF


PAIRING_VERSION = 1
IDENTITY_CURVE = ec.SECP256R1()
_KEY_BYTES = 16
_CCM_NONCE_BYTES = 13
_CCM_TAG_BYTES = 8
_LABEL_CONFIRM = b"stopwatch/pair/confirm"
_LABEL_WRAP = b"stopwatch/pair/wrap"
_LABEL_ACK = b"stopwatch/pair/ack"


@dataclass(frozen=True)
class PairingKeys:
    confirm_key: bytes
    wrap_key: bytes
    ack_key: bytes


def public_key_bytes(public_key: ec.EllipticCurvePublicKey) -> bytes:
    if not isinstance(public_key.curve, ec.SECP256R1):
        raise ValueError("pairing requires a P-256 public key")
    return public_key.public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.CompressedPoint,
    )


def _load_public_key(encoded: bytes) -> ec.EllipticCurvePublicKey:
    if len(encoded) != 33 or encoded[0] not in (2, 3):
        raise ValueError("public key must be a compressed P-256 point")
    try:
        return ec.EllipticCurvePublicKey.from_encoded_point(
            IDENTITY_CURVE, encoded
        )
    except ValueError as error:
        raise ValueError("public key is not on P-256") from error


def derive_shared_secret(
    private_key: ec.EllipticCurvePrivateKey, peer_public_key: bytes
) -> bytes:
    if not isinstance(private_key.curve, ec.SECP256R1):
        raise ValueError("pairing requires a P-256 private key")
    return private_key.exchange(ec.ECDH(), _load_public_key(peer_public_key))


def _derive_key(shared_secret: bytes, transcript: bytes, label: bytes) -> bytes:
    if len(shared_secret) != 32 or not transcript:
        raise ValueError("invalid ECDH secret or transcript")
    return HKDF(
        algorithm=hashes.SHA256(),
        length=_KEY_BYTES,
        salt=hashlib.sha256(transcript).digest(),
        info=label,
    ).derive(shared_secret)


def derive_pairing_keys(shared_secret: bytes, transcript: bytes) -> PairingKeys:
    return PairingKeys(
        confirm_key=_derive_key(shared_secret, transcript, _LABEL_CONFIRM),
        wrap_key=_derive_key(shared_secret, transcript, _LABEL_WRAP),
        ack_key=_derive_key(shared_secret, transcript, _LABEL_ACK),
    )


def verification_code(confirm_key: bytes, transcript: bytes) -> str:
    if len(confirm_key) != _KEY_BYTES or not transcript:
        raise ValueError("invalid confirmation key or transcript")
    digest = hmac.new(confirm_key, transcript, hashlib.sha256).digest()
    return f"{int.from_bytes(digest[:4], 'big') % 1_000_000:06d}"


def sign_challenge(
    private_key: ec.EllipticCurvePrivateKey, challenge: bytes
) -> bytes:
    if not isinstance(private_key.curve, ec.SECP256R1) or not challenge:
        raise ValueError("invalid identity key or challenge")
    return private_key.sign(challenge, ec.ECDSA(hashes.SHA256()))


def verify_challenge(
    public_key: bytes, challenge: bytes, signature: bytes
) -> bool:
    if not challenge or not signature:
        return False
    try:
        _load_public_key(public_key).verify(
            signature, challenge, ec.ECDSA(hashes.SHA256())
        )
    except (InvalidSignature, ValueError):
        return False
    return True


def _validate_wrap_inputs(key: bytes, nonce: bytes, aad: bytes) -> None:
    if len(key) != _KEY_BYTES:
        raise ValueError("credential wrapping key must be 16 bytes")
    if len(nonce) != _CCM_NONCE_BYTES or not any(nonce):
        raise ValueError("credential nonce must be 13 nonzero bytes")
    if not aad:
        raise ValueError("credential AAD cannot be empty")


def wrap_credential(
    key: bytes, nonce: bytes, plaintext: bytes, aad: bytes
) -> bytes:
    _validate_wrap_inputs(key, nonce, aad)
    if not plaintext:
        raise ValueError("credential cannot be empty")
    return AESCCM(key, tag_length=_CCM_TAG_BYTES).encrypt(
        nonce, plaintext, aad
    )


def unwrap_credential(
    key: bytes, nonce: bytes, ciphertext: bytes, aad: bytes
) -> bytes:
    _validate_wrap_inputs(key, nonce, aad)
    if len(ciphertext) < _CCM_TAG_BYTES:
        raise ValueError("wrapped credential is truncated")
    return AESCCM(key, tag_length=_CCM_TAG_BYTES).decrypt(
        nonce, ciphertext, aad
    )
