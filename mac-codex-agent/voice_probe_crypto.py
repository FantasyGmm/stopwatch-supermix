from cryptography.hazmat.primitives.ciphers.aead import AESCCM


NONCE_BYTES = 13
AAD_BYTES = 14
PLAINTEXT_BYTES = 164
TAG_BYTES = 8
CIPHERTEXT_BYTES = PLAINTEXT_BYTES + TAG_BYTES

_DIAGNOSTIC_KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
_NONCE_DOMAIN = bytes.fromhex("101112")
_AES_CCM = AESCCM(_DIAGNOSTIC_KEY, tag_length=TAG_BYTES)


def derive_nonce(session: int, sequence: int, watch_ms: int) -> bytes:
    if not 1 <= session <= 0xFFFF:
        raise ValueError("session must be 1..65535")
    if not 0 <= sequence <= 0xFFFFFFFF:
        raise ValueError("sequence must be 0..4294967295")
    if not 0 <= watch_ms <= 0xFFFFFFFF:
        raise ValueError("watch_ms must be 0..4294967295")
    return (
        _NONCE_DOMAIN
        + session.to_bytes(2, "little")
        + sequence.to_bytes(4, "little")
        + watch_ms.to_bytes(4, "little")
    )


def _validate_inputs(aad: bytes, value: bytes, expected_size: int, nonce: bytes) -> None:
    if len(aad) != AAD_BYTES:
        raise ValueError(f"AAD must be {AAD_BYTES} bytes")
    if len(value) != expected_size:
        raise ValueError(f"value must be {expected_size} bytes")
    if len(nonce) != NONCE_BYTES or not any(nonce):
        raise ValueError(f"nonce must be {NONCE_BYTES} nonzero bytes")


def encrypt(aad: bytes, plaintext: bytes, nonce: bytes) -> bytes:
    _validate_inputs(aad, plaintext, PLAINTEXT_BYTES, nonce)
    return _AES_CCM.encrypt(nonce, plaintext, aad)


def decrypt(aad: bytes, ciphertext: bytes, nonce: bytes) -> bytes:
    _validate_inputs(aad, ciphertext, CIPHERTEXT_BYTES, nonce)
    return _AES_CCM.decrypt(nonce, ciphertext, aad)
