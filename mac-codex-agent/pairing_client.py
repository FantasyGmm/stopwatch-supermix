"""Bounded client state machine for secure watch enrollment and authentication."""

from dataclasses import dataclass
from enum import IntEnum
import hashlib
import secrets
import struct
from typing import Callable, Optional

from cryptography.hazmat.primitives.asymmetric import ec

from secure_pairing_crypto import (
    PairingKeys,
    derive_pairing_keys,
    derive_shared_secret,
    public_key_bytes,
    sign_challenge,
    unwrap_credential,
    verification_code,
    wrap_credential,
)
from secure_pairing_protocol import (
    PAIRING_VERSION,
    PairingTranscript,
    encode_pairing_transcript,
)


_WATCH_ID_BYTES = 16
_RANDOM_BYTES = 16
_KEY_BYTES = 16
_FINGERPRINT_BYTES = 16
_CCM_NONCE_BYTES = 13
_CREDENTIAL_MAGIC = b"SWCRED"
_ENVELOPE_MAGIC = b"SWENV"
_ACK_MAGIC = b"SWACK"
_AUTH_MAGIC = b"SWAUTH"
_AUTH_CHALLENGE = struct.Struct(">6sBBI16s16sI")
_CREDENTIAL = struct.Struct(">6sB16sI16s16s")
_ENVELOPE_AAD = struct.Struct(">5sBBI")
_ACK_AAD = struct.Struct(">5sBI")


class CredentialPurpose(IntEnum):
    PAIRING = 1
    REKEY = 2


@dataclass(frozen=True)
class BeginPairingRequest:
    transaction_id: int
    agent_nonce: bytes
    agent_ephemeral_public: bytes
    agent_identity_public: bytes
    display_name: str


@dataclass(frozen=True)
class PairingOffer:
    transaction_id: int
    watch_id: bytes
    watch_nonce: bytes
    watch_ephemeral_public: bytes


@dataclass(frozen=True)
class AuthenticationRequest:
    transaction_id: int
    fingerprint: bytes
    agent_key_epoch: int


@dataclass(frozen=True)
class AuthenticationChallenge:
    transaction_id: int
    watch_id: bytes
    connection_nonce: bytes
    key_epoch: int
    connection_role: int


@dataclass(frozen=True)
class CredentialEnvelope:
    purpose: CredentialPurpose
    transaction_id: int
    key_epoch: int
    watch_ephemeral_public: bytes
    nonce: bytes
    ciphertext: bytes
    ack_nonce: bytes


@dataclass(frozen=True)
class WatchCredential:
    watch_id: bytes
    master_key: bytes
    key_epoch: int
    fingerprint: bytes


@dataclass(frozen=True)
class PairingPresentation:
    transaction_id: int
    watch_id: bytes
    verification_code: str


@dataclass(frozen=True)
class PairingResult:
    paired: bool
    credential: Optional[WatchCredential]


@dataclass(frozen=True)
class AuthenticationResult:
    authenticated: bool
    rekeyed: bool
    credential: WatchCredential


@dataclass
class _PendingPairing:
    transport: object
    transaction_id: int
    watch_id: bytes
    fingerprint: bytes
    keys: PairingKeys


@dataclass
class _PendingAuthentication:
    transport: object
    identity: ec.EllipticCurvePrivateKey
    credential: WatchCredential
    challenge: AuthenticationChallenge
    encoded_challenge: bytes


def _valid_transaction_id(value: int) -> bool:
    return isinstance(value, int) and 0 < value <= 0xFFFFFFFF


def _require_bytes(value: bytes, size: int, label: str, nonzero=True) -> bytes:
    if not isinstance(value, bytes) or len(value) != size:
        raise ValueError("{} must be {} bytes".format(label, size))
    if nonzero and not any(value):
        raise ValueError("{} cannot be zero".format(label))
    return value


def _fingerprint(identity_public: bytes) -> bytes:
    _require_bytes(identity_public, 33, "identity public key")
    return hashlib.sha256(identity_public).digest()[:_FINGERPRINT_BYTES]


def encode_authentication_challenge(
    challenge: AuthenticationChallenge,
) -> bytes:
    if not _valid_transaction_id(challenge.transaction_id):
        raise ValueError("authentication transaction is invalid")
    _require_bytes(challenge.watch_id, _WATCH_ID_BYTES, "watch identifier")
    _require_bytes(
        challenge.connection_nonce, _RANDOM_BYTES, "connection nonce"
    )
    if challenge.key_epoch <= 0 or challenge.key_epoch > 0xFFFFFFFF:
        raise ValueError("authentication key epoch is invalid")
    if challenge.connection_role != 1:
        raise ValueError("authentication role is invalid")
    return _AUTH_CHALLENGE.pack(
        _AUTH_MAGIC,
        PAIRING_VERSION,
        challenge.connection_role,
        challenge.transaction_id,
        challenge.watch_id,
        challenge.connection_nonce,
        challenge.key_epoch,
    )


def encode_credential_plaintext(credential: WatchCredential) -> bytes:
    _require_bytes(credential.watch_id, _WATCH_ID_BYTES, "watch identifier")
    _require_bytes(credential.master_key, _KEY_BYTES, "watch master key")
    _require_bytes(
        credential.fingerprint, _FINGERPRINT_BYTES, "Agent fingerprint"
    )
    if credential.key_epoch <= 0 or credential.key_epoch > 0xFFFFFFFF:
        raise ValueError("credential key epoch is invalid")
    return _CREDENTIAL.pack(
        _CREDENTIAL_MAGIC,
        PAIRING_VERSION,
        credential.master_key,
        credential.key_epoch,
        credential.watch_id,
        credential.fingerprint,
    )


def decode_credential_plaintext(data: bytes) -> WatchCredential:
    if len(data) != _CREDENTIAL.size:
        raise ValueError("credential plaintext has invalid size")
    magic, version, master_key, key_epoch, watch_id, fingerprint = (
        _CREDENTIAL.unpack(data)
    )
    if magic != _CREDENTIAL_MAGIC or version != PAIRING_VERSION:
        raise ValueError("credential plaintext has invalid version")
    return WatchCredential(
        watch_id=watch_id,
        master_key=master_key,
        key_epoch=key_epoch,
        fingerprint=fingerprint,
    )


def encode_envelope_aad(envelope: CredentialEnvelope) -> bytes:
    if not _valid_transaction_id(envelope.transaction_id):
        raise ValueError("credential transaction is invalid")
    try:
        purpose = CredentialPurpose(envelope.purpose)
    except ValueError as error:
        raise ValueError("credential purpose is invalid") from error
    return _ENVELOPE_AAD.pack(
        _ENVELOPE_MAGIC,
        PAIRING_VERSION,
        int(purpose),
        envelope.transaction_id,
    )


def encode_credential_ack(
    ack_key: bytes, envelope: CredentialEnvelope
) -> bytes:
    aad = _ACK_AAD.pack(
        _ACK_MAGIC, PAIRING_VERSION, envelope.transaction_id
    )
    return wrap_credential(
        ack_key,
        _require_bytes(
            envelope.ack_nonce, _CCM_NONCE_BYTES, "acknowledgement nonce"
        ),
        struct.pack(">I", envelope.transaction_id),
        aad,
    )


class PairingClient:
    def __init__(
        self,
        credential_store,
        *,
        random_bytes: Callable[[int], bytes] = secrets.token_bytes,
        transaction_id: Callable[[], int] = lambda: secrets.randbelow(
            0xFFFFFFFF
        )
        + 1,
    ):
        self._credential_store = credential_store
        self._random_bytes = random_bytes
        self._transaction_id = transaction_id
        self._pending_pairing = None
        self._pending_authentication = None

    async def request_pairing(
        self,
        transport,
        identity: ec.EllipticCurvePrivateKey,
        display_name: str,
    ) -> PairingPresentation:
        if self._pending_pairing is not None:
            raise RuntimeError("a pairing request is already pending")
        identity_public = public_key_bytes(identity.public_key())
        fingerprint = _fingerprint(identity_public)
        ephemeral = ec.generate_private_key(ec.SECP256R1())
        transaction_id = self._new_transaction_id()
        agent_nonce = _require_bytes(
            self._random_bytes(_RANDOM_BYTES),
            _RANDOM_BYTES,
            "Agent nonce",
        )
        request = BeginPairingRequest(
            transaction_id=transaction_id,
            agent_nonce=agent_nonce,
            agent_ephemeral_public=public_key_bytes(ephemeral.public_key()),
            agent_identity_public=identity_public,
            display_name=display_name,
        )
        offer = await transport.request_pairing(request)
        if (
            offer.transaction_id != transaction_id
            or len(offer.watch_ephemeral_public) != 33
        ):
            raise ValueError("watch returned a mismatched pairing offer")
        _require_bytes(offer.watch_id, _WATCH_ID_BYTES, "watch identifier")
        _require_bytes(offer.watch_nonce, _RANDOM_BYTES, "watch nonce")
        transcript = PairingTranscript(
            transaction_id=transaction_id,
            watch_id=offer.watch_id,
            watch_nonce=offer.watch_nonce,
            agent_nonce=agent_nonce,
            watch_ephemeral_public=offer.watch_ephemeral_public,
            agent_ephemeral_public=request.agent_ephemeral_public,
            agent_identity_public=identity_public,
            agent_name=display_name,
        )
        encoded = encode_pairing_transcript(transcript)
        shared = derive_shared_secret(
            ephemeral, offer.watch_ephemeral_public
        )
        keys = derive_pairing_keys(shared, encoded)
        self._pending_pairing = _PendingPairing(
            transport=transport,
            transaction_id=transaction_id,
            watch_id=offer.watch_id,
            fingerprint=fingerprint,
            keys=keys,
        )
        return PairingPresentation(
            transaction_id=transaction_id,
            watch_id=offer.watch_id,
            verification_code=verification_code(
                keys.confirm_key, encoded
            ),
        )

    async def confirm_pairing(self, accepted: bool) -> PairingResult:
        pending = self._pending_pairing
        if pending is None:
            raise RuntimeError("no pairing request is pending")
        try:
            envelope = await pending.transport.confirm_pairing(
                pending.transaction_id, bool(accepted)
            )
            if not accepted:
                if envelope is not None:
                    raise ValueError(
                        "watch returned credentials for rejected pairing"
                    )
                return PairingResult(False, None)
            if envelope is None:
                raise ValueError("watch omitted the pairing credential")
            credential = self._unwrap_envelope(
                envelope,
                pending.keys,
                CredentialPurpose.PAIRING,
                pending.transaction_id,
                pending.watch_id,
                pending.fingerprint,
            )
            self._credential_store.save(credential)
            proof = encode_credential_ack(pending.keys.ack_key, envelope)
            await pending.transport.acknowledge_credential(
                pending.transaction_id, proof
            )
            return PairingResult(True, credential)
        finally:
            self._pending_pairing = None

    async def authenticate(
        self,
        transport,
        identity: ec.EllipticCurvePrivateKey,
        credential: WatchCredential,
    ) -> AuthenticationResult:
        if self._pending_authentication is not None:
            raise RuntimeError("an authentication request is already pending")
        identity_public = public_key_bytes(identity.public_key())
        if _fingerprint(identity_public) != credential.fingerprint:
            raise ValueError("credential belongs to another Agent identity")
        request = AuthenticationRequest(
            transaction_id=self._new_transaction_id(),
            fingerprint=credential.fingerprint,
            agent_key_epoch=credential.key_epoch,
        )
        challenge = await transport.begin_authentication(request)
        if (
            challenge.transaction_id != request.transaction_id
            or challenge.watch_id != credential.watch_id
        ):
            raise ValueError("watch returned a mismatched challenge")
        encoded = encode_authentication_challenge(challenge)
        self._pending_authentication = _PendingAuthentication(
            transport=transport,
            identity=identity,
            credential=credential,
            challenge=challenge,
            encoded_challenge=encoded,
        )
        try:
            signature = sign_challenge(identity, encoded)
            envelope = await transport.authenticate(
                request.transaction_id, signature
            )
            if envelope is None:
                if challenge.key_epoch != credential.key_epoch:
                    raise ValueError("watch omitted required rekey credential")
                return AuthenticationResult(
                    authenticated=True,
                    rekeyed=False,
                    credential=credential,
                )
            updated = await self.accept_rekey(envelope)
            return AuthenticationResult(
                authenticated=True,
                rekeyed=True,
                credential=updated,
            )
        finally:
            self._pending_authentication = None

    async def accept_rekey(
        self, envelope: CredentialEnvelope
    ) -> WatchCredential:
        pending = self._pending_authentication
        if pending is None:
            raise RuntimeError("no authenticated rekey is pending")
        shared = derive_shared_secret(
            pending.identity, envelope.watch_ephemeral_public
        )
        keys = derive_pairing_keys(shared, pending.encoded_challenge)
        credential = self._unwrap_envelope(
            envelope,
            keys,
            CredentialPurpose.REKEY,
            pending.challenge.transaction_id,
            pending.credential.watch_id,
            pending.credential.fingerprint,
        )
        if credential.key_epoch <= pending.credential.key_epoch:
            raise ValueError("rekey epoch did not advance")
        self._credential_store.save(credential)
        proof = encode_credential_ack(keys.ack_key, envelope)
        await pending.transport.acknowledge_credential(
            pending.challenge.transaction_id, proof
        )
        return credential

    def _unwrap_envelope(
        self,
        envelope: CredentialEnvelope,
        keys: PairingKeys,
        expected_purpose: CredentialPurpose,
        expected_transaction: int,
        expected_watch_id: bytes,
        expected_fingerprint: bytes,
    ) -> WatchCredential:
        if (
            CredentialPurpose(envelope.purpose) != expected_purpose
            or envelope.transaction_id != expected_transaction
            or envelope.key_epoch <= 0
        ):
            raise ValueError("credential envelope does not match request")
        _require_bytes(envelope.nonce, _CCM_NONCE_BYTES, "credential nonce")
        _require_bytes(
            envelope.ack_nonce,
            _CCM_NONCE_BYTES,
            "acknowledgement nonce",
        )
        plaintext = unwrap_credential(
            keys.wrap_key,
            envelope.nonce,
            envelope.ciphertext,
            encode_envelope_aad(envelope),
        )
        credential = decode_credential_plaintext(plaintext)
        if (
            credential.key_epoch != envelope.key_epoch
            or credential.watch_id != expected_watch_id
            or credential.fingerprint != expected_fingerprint
        ):
            raise ValueError("wrapped credential does not match identity")
        return credential

    def _new_transaction_id(self) -> int:
        value = self._transaction_id()
        if not _valid_transaction_id(value):
            raise ValueError("transaction identifier source returned invalid data")
        return value
