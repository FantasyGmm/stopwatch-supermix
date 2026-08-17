"""AES-CCM and Keychain boundaries for production StopWatch voice input."""

import re
import subprocess
from collections.abc import Callable

from cryptography.hazmat.primitives.ciphers.aead import AESCCM


KEY_BYTES = 16
EXCHANGE_NONCE_BYTES = 8
NONCE_BYTES = 13
TAG_BYTES = 8

READY_DOMAIN = 0x01
UNAVAILABLE_DOMAIN = 0x02
PTT_BEGIN_DOMAIN = 0x11
AUDIO_DOMAIN = 0x12
PTT_END_DOMAIN = 0x13
VOICE_ERROR_DOMAIN = 0x14
INPUT_READY_DOMAIN = 0x21
INPUT_SUBMIT_DOMAIN = 0x22
INPUT_FORCE_STEER_DOMAIN = 0x23
TASK_DETAIL_DOMAIN = 0x24

DOMAINS = frozenset(
    {
        READY_DOMAIN,
        UNAVAILABLE_DOMAIN,
        PTT_BEGIN_DOMAIN,
        AUDIO_DOMAIN,
        PTT_END_DOMAIN,
        VOICE_ERROR_DOMAIN,
        INPUT_READY_DOMAIN,
        INPUT_SUBMIT_DOMAIN,
        INPUT_FORCE_STEER_DOMAIN,
        TASK_DETAIL_DOMAIN,
    }
)

KEYCHAIN_SERVICE = "com.stopwatch.codex-agent.voice"
KEYCHAIN_ACCOUNT = "StopWatch-Codex"
KEYCHAIN_TIMEOUT_SECONDS = 2.0
_KEY_PATTERN = re.compile(r"[0-9a-f]{32}")


class VoiceKeyError(RuntimeError):
    """Raised when the production voice key cannot be loaded safely."""


def _validate_key(key: bytes) -> None:
    if not isinstance(key, bytes) or len(key) != KEY_BYTES:
        raise ValueError(f"key must be {KEY_BYTES} bytes")


def validate_key_epoch(key_epoch: int) -> None:
    if not isinstance(key_epoch, int) or not 1 <= key_epoch <= 0xFFFFFFFF:
        raise ValueError("key epoch must be 1..4294967295")


def derive_nonce(domain: int, exchange_nonce: bytes, sequence: int) -> bytes:
    if domain not in DOMAINS:
        raise ValueError("unknown voice nonce domain")
    if (
        not isinstance(exchange_nonce, bytes)
        or len(exchange_nonce) != EXCHANGE_NONCE_BYTES
        or not any(exchange_nonce)
    ):
        raise ValueError("exchange nonce must be 8 nonzero bytes")
    if not 0 <= sequence <= 0xFFFFFFFF:
        raise ValueError("sequence must be 0..4294967295")
    return bytes((domain,)) + exchange_nonce + sequence.to_bytes(4, "little")


def encrypt(
    key: bytes,
    domain: int,
    exchange_nonce: bytes,
    sequence: int,
    plaintext: bytes,
    aad: bytes,
) -> bytes:
    _validate_key(key)
    nonce = derive_nonce(domain, exchange_nonce, sequence)
    return AESCCM(key, tag_length=TAG_BYTES).encrypt(nonce, plaintext, aad)


def decrypt(
    key: bytes,
    domain: int,
    exchange_nonce: bytes,
    sequence: int,
    ciphertext: bytes,
    aad: bytes,
) -> bytes:
    _validate_key(key)
    nonce = derive_nonce(domain, exchange_nonce, sequence)
    return AESCCM(key, tag_length=TAG_BYTES).decrypt(nonce, ciphertext, aad)


def load_key_from_keychain(
    runner: Callable[..., subprocess.CompletedProcess] = subprocess.run,
) -> bytes:
    command = [
        "security",
        "find-generic-password",
        "-s",
        KEYCHAIN_SERVICE,
        "-a",
        KEYCHAIN_ACCOUNT,
        "-w",
    ]
    try:
        result = runner(
            command,
            capture_output=True,
            text=True,
            check=False,
            timeout=KEYCHAIN_TIMEOUT_SECONDS,
        )
    except (OSError, subprocess.TimeoutExpired):
        raise VoiceKeyError("voice key is unavailable from Keychain") from None
    if result.returncode != 0:
        raise VoiceKeyError("voice key is unavailable from Keychain")

    encoded_key = result.stdout.rstrip("\r\n")
    if _KEY_PATTERN.fullmatch(encoded_key) is None:
        raise VoiceKeyError("voice key stored in Keychain is malformed")
    return bytes.fromhex(encoded_key)
