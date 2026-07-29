"""Long-term Mac Agent identity and display-name Keychain storage."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import socket
import struct
import unicodedata
from typing import Callable, Optional, Protocol

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

from secure_pairing_crypto import public_key_bytes


IDENTITY_SERVICE = "com.stopwatch.codex-agent.identity"
IDENTITY_NAME_SERVICE = "com.stopwatch.codex-agent.identity-name"
KEYCHAIN_ACCOUNT = "default"
MAX_AGENT_NAME_BYTES = 24

_IDENTITY_MAGIC = b"SWID"
_IDENTITY_VERSION = 1
_IDENTITY_HEADER = struct.Struct(">4sBH")
_NAME_MAGIC = b"SWNM"
_NAME_VERSION = 1
_NAME_HEADER = struct.Struct(">4sBB")


class AgentIdentityError(RuntimeError):
    """A redacted identity-storage failure."""


class KeychainAdapter(Protocol):
    def read(self, service: str, account: str) -> Optional[bytes]:
        ...

    def replace(self, service: str, account: str, value: bytes) -> None:
        ...

    def delete(self, service: str, account: str) -> None:
        ...


class MacOSKeychain:
    """Generic-password storage backed directly by Security.framework."""

    _NOT_FOUND = -25300
    _DUPLICATE = -25299

    @staticmethod
    def _framework():
        try:
            import Security
        except ImportError:
            raise AgentIdentityError("macOS Keychain framework is unavailable") from None
        return Security

    @staticmethod
    def _query(security, service: str, account: str):
        return {
            security.kSecClass: security.kSecClassGenericPassword,
            security.kSecAttrService: service,
            security.kSecAttrAccount: account,
        }

    def read(self, service: str, account: str) -> Optional[bytes]:
        security = self._framework()
        query = self._query(security, service, account)
        query.update(
            {
                security.kSecReturnData: True,
                security.kSecMatchLimit: security.kSecMatchLimitOne,
            }
        )
        status, result = security.SecItemCopyMatching(query, None)
        if status == self._NOT_FOUND:
            return None
        if status != 0 or result is None:
            raise AgentIdentityError("macOS Keychain read failed")
        return bytes(result)

    def replace(self, service: str, account: str, value: bytes) -> None:
        security = self._framework()
        query = self._query(security, service, account)
        status = security.SecItemUpdate(
            query, {security.kSecValueData: bytes(value)}
        )
        if status == self._NOT_FOUND:
            attributes = dict(query)
            attributes.update(
                {
                    security.kSecValueData: bytes(value),
                    security.kSecAttrAccessible: (
                        security.kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
                    ),
                }
            )
            status, _ = security.SecItemAdd(attributes, None)
            if status == self._DUPLICATE:
                status = security.SecItemUpdate(
                    query, {security.kSecValueData: bytes(value)}
                )
        if status != 0:
            raise AgentIdentityError("macOS Keychain update failed")

    def delete(self, service: str, account: str) -> None:
        security = self._framework()
        status = security.SecItemDelete(self._query(security, service, account))
        if status not in (0, self._NOT_FOUND):
            raise AgentIdentityError("macOS Keychain delete failed")


@dataclass(frozen=True)
class AgentIdentity:
    private_key: ec.EllipticCurvePrivateKey
    public_key: bytes
    fingerprint: bytes
    display_name: str


@dataclass(frozen=True)
class AgentIdentitySnapshot:
    public_key: bytes
    fingerprint: bytes
    display_name: str


def normalize_agent_name(value: str) -> str:
    if not isinstance(value, str):
        raise ValueError("Agent name must be text")
    normalized = unicodedata.normalize("NFC", value)
    clean = []
    pending_space = False
    for character in normalized:
        if character.isspace():
            pending_space = bool(clean)
            continue
        if unicodedata.category(character).startswith("C"):
            raise ValueError("Agent name contains unsupported characters")
        if pending_space:
            clean.append(" ")
            pending_space = False
        clean.append(character)
    candidate = "".join(clean)
    if not candidate:
        raise ValueError("Agent name cannot be empty")

    bounded = bytearray()
    output = []
    for character in candidate:
        encoded = character.encode("utf-8")
        if len(bounded) + len(encoded) > MAX_AGENT_NAME_BYTES:
            break
        bounded.extend(encoded)
        output.append(character)
    result = "".join(output).rstrip()
    if not result:
        raise ValueError("Agent name cannot be empty")
    return result


def _default_agent_name() -> str:
    hostname = socket.gethostname().split(".", 1)[0]
    try:
        return normalize_agent_name(hostname)
    except ValueError:
        return "Mac Agent"


def _encode_identity(private_key: ec.EllipticCurvePrivateKey) -> bytes:
    encoded = private_key.private_bytes(
        serialization.Encoding.DER,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    )
    if len(encoded) > 0xFFFF:
        raise AgentIdentityError("Agent identity cannot be encoded")
    return _IDENTITY_HEADER.pack(
        _IDENTITY_MAGIC, _IDENTITY_VERSION, len(encoded)
    ) + encoded


def _decode_identity(payload: bytes) -> ec.EllipticCurvePrivateKey:
    if len(payload) < _IDENTITY_HEADER.size:
        raise ValueError("invalid identity record")
    magic, version, encoded_size = _IDENTITY_HEADER.unpack_from(payload)
    encoded = payload[_IDENTITY_HEADER.size :]
    if (
        magic != _IDENTITY_MAGIC
        or version != _IDENTITY_VERSION
        or encoded_size != len(encoded)
    ):
        raise ValueError("invalid identity record")
    key = serialization.load_der_private_key(encoded, password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(
        key.curve, ec.SECP256R1
    ):
        raise ValueError("invalid identity record")
    return key


def _encode_name(display_name: str) -> bytes:
    encoded = normalize_agent_name(display_name).encode("utf-8")
    return _NAME_HEADER.pack(_NAME_MAGIC, _NAME_VERSION, len(encoded)) + encoded


def _decode_name(payload: bytes) -> str:
    if len(payload) < _NAME_HEADER.size:
        raise ValueError("invalid Agent name record")
    magic, version, encoded_size = _NAME_HEADER.unpack_from(payload)
    encoded = payload[_NAME_HEADER.size :]
    if (
        magic != _NAME_MAGIC
        or version != _NAME_VERSION
        or encoded_size != len(encoded)
    ):
        raise ValueError("invalid Agent name record")
    return normalize_agent_name(encoded.decode("utf-8"))


class AgentIdentityStore:
    def __init__(
        self,
        keychain: Optional[KeychainAdapter] = None,
        default_name: Callable[[], str] = _default_agent_name,
    ):
        self._keychain = keychain or MacOSKeychain()
        self._default_name = default_name
        self._identity: Optional[AgentIdentity] = None

    def load_or_create(self) -> AgentIdentity:
        if self._identity is not None:
            return self._identity
        try:
            payload = self._keychain.read(IDENTITY_SERVICE, KEYCHAIN_ACCOUNT)
            if payload is None:
                private_key = ec.generate_private_key(ec.SECP256R1())
                self._keychain.replace(
                    IDENTITY_SERVICE,
                    KEYCHAIN_ACCOUNT,
                    _encode_identity(private_key),
                )
            else:
                private_key = _decode_identity(payload)
            display_name = self._load_or_create_name()
        except Exception:
            raise AgentIdentityError(
                "Agent identity is unavailable from Keychain"
            ) from None

        public_key = public_key_bytes(private_key.public_key())
        self._identity = AgentIdentity(
            private_key=private_key,
            public_key=public_key,
            fingerprint=hashlib.sha256(public_key).digest()[:16],
            display_name=display_name,
        )
        return self._identity

    def _load_or_create_name(self) -> str:
        payload = self._keychain.read(IDENTITY_NAME_SERVICE, KEYCHAIN_ACCOUNT)
        if payload is None:
            display_name = normalize_agent_name(self._default_name())
            self._keychain.replace(
                IDENTITY_NAME_SERVICE,
                KEYCHAIN_ACCOUNT,
                _encode_name(display_name),
            )
            return display_name
        return _decode_name(payload)

    def rename(self, value: str) -> str:
        display_name = normalize_agent_name(value)
        identity = self.load_or_create()
        try:
            self._keychain.replace(
                IDENTITY_NAME_SERVICE,
                KEYCHAIN_ACCOUNT,
                _encode_name(display_name),
            )
        except Exception:
            raise AgentIdentityError(
                "Agent name is unavailable from Keychain"
            ) from None
        self._identity = AgentIdentity(
            private_key=identity.private_key,
            public_key=identity.public_key,
            fingerprint=identity.fingerprint,
            display_name=display_name,
        )
        return display_name

    def snapshot(self) -> AgentIdentitySnapshot:
        identity = self.load_or_create()
        return AgentIdentitySnapshot(
            public_key=identity.public_key,
            fingerprint=identity.fingerprint,
            display_name=identity.display_name,
        )
