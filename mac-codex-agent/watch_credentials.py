"""Atomic per-watch master-key and epoch records stored in Keychain."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import hmac
import struct
from typing import Optional

from agent_identity import KeychainAdapter, MacOSKeychain
from pairing_client import WatchCredential


WATCH_SERVICE_PREFIX = "com.stopwatch.codex-agent.watch."
WATCH_ACCOUNT = "credential"

_MAGIC = b"SWCRED"
_VERSION = 1
_RECORD = struct.Struct(">6sB16sI16s16s")
_CHECKSUM_BYTES = 16


class WatchCredentialError(RuntimeError):
    """A redacted watch-credential storage failure."""


class StaleCredentialError(WatchCredentialError):
    """A credential would roll back or fork an accepted epoch."""


@dataclass(frozen=True)
class WatchCredentialSnapshot:
    watch_id_suffix: str
    key_epoch: int
    fingerprint_suffix: str


def _require_bytes(value: bytes, size: int, label: str) -> bytes:
    if not isinstance(value, bytes) or len(value) != size or not any(value):
        raise ValueError("{} must be {} nonzero bytes".format(label, size))
    return value


def _validate(credential: WatchCredential) -> None:
    _require_bytes(credential.watch_id, 16, "watch identifier")
    _require_bytes(credential.master_key, 16, "watch master key")
    _require_bytes(credential.fingerprint, 16, "Agent fingerprint")
    if (
        not isinstance(credential.key_epoch, int)
        or isinstance(credential.key_epoch, bool)
        or credential.key_epoch <= 0
        or credential.key_epoch > 0xFFFFFFFF
    ):
        raise ValueError("watch key epoch is invalid")


def _service(watch_id: bytes) -> str:
    _require_bytes(watch_id, 16, "watch identifier")
    return WATCH_SERVICE_PREFIX + watch_id.hex()


def _encode(credential: WatchCredential) -> bytes:
    _validate(credential)
    record = _RECORD.pack(
        _MAGIC,
        _VERSION,
        credential.watch_id,
        credential.key_epoch,
        credential.master_key,
        credential.fingerprint,
    )
    return record + hashlib.sha256(record).digest()[:_CHECKSUM_BYTES]


def _decode(payload: bytes, expected_watch_id: bytes) -> WatchCredential:
    if len(payload) != _RECORD.size + _CHECKSUM_BYTES:
        raise ValueError("invalid credential record")
    record = payload[: _RECORD.size]
    checksum = payload[_RECORD.size :]
    expected_checksum = hashlib.sha256(record).digest()[:_CHECKSUM_BYTES]
    if not hmac.compare_digest(checksum, expected_checksum):
        raise ValueError("invalid credential record")
    magic, version, watch_id, key_epoch, master_key, fingerprint = (
        _RECORD.unpack(record)
    )
    credential = WatchCredential(
        watch_id=watch_id,
        master_key=master_key,
        key_epoch=key_epoch,
        fingerprint=fingerprint,
    )
    _validate(credential)
    if magic != _MAGIC or version != _VERSION or watch_id != expected_watch_id:
        raise ValueError("invalid credential record")
    return credential


class WatchCredentialStore:
    def __init__(self, keychain: Optional[KeychainAdapter] = None):
        self._keychain = keychain or MacOSKeychain()

    def load(self, watch_id: bytes) -> Optional[WatchCredential]:
        service = _service(watch_id)
        try:
            payload = self._keychain.read(service, WATCH_ACCOUNT)
            return None if payload is None else _decode(payload, watch_id)
        except Exception:
            raise WatchCredentialError(
                "watch credential is unavailable from Keychain"
            ) from None

    def replace(self, credential: WatchCredential) -> None:
        _validate(credential)
        current = self.load(credential.watch_id)
        if current is not None:
            if credential.key_epoch < current.key_epoch:
                raise StaleCredentialError("watch credential epoch is stale")
            if credential.key_epoch == current.key_epoch and credential != current:
                raise StaleCredentialError("watch credential epoch conflicts")
            if credential == current:
                return
        try:
            self._keychain.replace(
                _service(credential.watch_id), WATCH_ACCOUNT, _encode(credential)
            )
        except Exception:
            raise WatchCredentialError(
                "watch credential is unavailable from Keychain"
            ) from None

    def save(self, credential: WatchCredential) -> None:
        """PairingClient-compatible atomic credential commit."""
        self.replace(credential)

    def forget(self, watch_id: bytes) -> None:
        try:
            self._keychain.delete(_service(watch_id), WATCH_ACCOUNT)
        except Exception:
            raise WatchCredentialError(
                "watch credential is unavailable from Keychain"
            ) from None

    def snapshot(self, watch_id: bytes) -> Optional[WatchCredentialSnapshot]:
        credential = self.load(watch_id)
        if credential is None:
            return None
        return WatchCredentialSnapshot(
            watch_id_suffix=credential.watch_id.hex()[-4:].upper(),
            key_epoch=credential.key_epoch,
            fingerprint_suffix=credential.fingerprint.hex()[-4:].upper(),
        )
