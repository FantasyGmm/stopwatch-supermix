import hashlib
import os
import unittest

from cryptography.hazmat.primitives.asymmetric import ec

from pairing_client import (
    AuthenticationChallenge,
    CredentialEnvelope,
    CredentialPurpose,
    PairingClient,
    PairingOffer,
    WatchCredential,
    encode_authentication_challenge,
    encode_credential_ack,
    encode_credential_plaintext,
    encode_envelope_aad,
)
from secure_pairing_crypto import (
    derive_pairing_keys,
    derive_shared_secret,
    public_key_bytes,
    unwrap_credential,
    verify_challenge,
    wrap_credential,
)
from secure_pairing_protocol import PairingTranscript, encode_pairing_transcript


class CredentialStore:
    def __init__(self):
        self.saved = []
        self.events = []
        self.failure = None

    def save(self, credential):
        self.events.append("store")
        if self.failure is not None:
            raise self.failure
        self.saved.append(credential)


class FakeWatch:
    def __init__(self, identity_public, store):
        self.identity_public = identity_public
        self.store = store
        self.watch_id = bytes(range(16))
        self.master_key = bytes(range(16, 32))
        self.key_epoch = 3
        self.fingerprint = hashlib.sha256(identity_public).digest()[:16]
        self._pairing_keys = None
        self._pairing_request = None
        self._auth_challenge = None
        self._auth_keys = None
        self.acks = []
        self.tamper_envelope = False

    async def request_pairing(self, request):
        self._pairing_request = request
        watch_private = ec.derive_private_key(31, ec.SECP256R1())
        self._watch_pairing_private = watch_private
        offer = PairingOffer(
            transaction_id=request.transaction_id,
            watch_id=self.watch_id,
            watch_nonce=bytes(range(64, 80)),
            watch_ephemeral_public=public_key_bytes(watch_private.public_key()),
        )
        transcript = PairingTranscript(
            transaction_id=request.transaction_id,
            watch_id=offer.watch_id,
            watch_nonce=offer.watch_nonce,
            agent_nonce=request.agent_nonce,
            watch_ephemeral_public=offer.watch_ephemeral_public,
            agent_ephemeral_public=request.agent_ephemeral_public,
            agent_identity_public=request.agent_identity_public,
            agent_name=request.display_name,
        )
        shared = derive_shared_secret(
            watch_private, request.agent_ephemeral_public
        )
        self._pairing_keys = derive_pairing_keys(
            shared, encode_pairing_transcript(transcript)
        )
        return offer

    def _envelope(self, purpose, transaction_id, keys, watch_public=b""):
        envelope = CredentialEnvelope(
            purpose=purpose,
            transaction_id=transaction_id,
            key_epoch=self.key_epoch,
            watch_ephemeral_public=watch_public,
            nonce=bytes(range(80, 93)),
            ciphertext=b"",
            ack_nonce=bytes(range(93, 106)),
        )
        plaintext = encode_credential_plaintext(
            WatchCredential(
                watch_id=self.watch_id,
                master_key=self.master_key,
                key_epoch=self.key_epoch,
                fingerprint=self.fingerprint,
            )
        )
        ciphertext = wrap_credential(
            keys.wrap_key,
            envelope.nonce,
            plaintext,
            encode_envelope_aad(envelope),
        )
        if self.tamper_envelope:
            ciphertext = ciphertext[:-1] + bytes((ciphertext[-1] ^ 1,))
        return CredentialEnvelope(
            purpose=envelope.purpose,
            transaction_id=envelope.transaction_id,
            key_epoch=envelope.key_epoch,
            watch_ephemeral_public=envelope.watch_ephemeral_public,
            nonce=envelope.nonce,
            ciphertext=ciphertext,
            ack_nonce=envelope.ack_nonce,
        )

    async def confirm_pairing(self, transaction_id, accepted):
        if not accepted:
            return None
        return self._envelope(
            CredentialPurpose.PAIRING,
            transaction_id,
            self._pairing_keys,
        )

    async def acknowledge_credential(self, transaction_id, proof):
        self.store.events.append("ack")
        self.acks.append((transaction_id, proof))
        keys = self._auth_keys or self._pairing_keys
        envelope = self._last_auth_envelope
        expected = encode_credential_ack(keys.ack_key, envelope)
        if proof != expected:
            raise ValueError("invalid acknowledgement")

    async def request_authentication(self, request):
        self._auth_challenge = AuthenticationChallenge(
            transaction_id=request.transaction_id,
            watch_id=self.watch_id,
            connection_nonce=bytes(range(106, 122)),
            key_epoch=self.key_epoch,
            connection_role=1,
        )
        return self._auth_challenge

    async def authenticate(self, transaction_id, signature):
        encoded = encode_authentication_challenge(self._auth_challenge)
        if not verify_challenge(self.identity_public, encoded, signature):
            raise ValueError("identity signature rejected")
        if self._requested_epoch == self.key_epoch:
            return None
        watch_private = ec.derive_private_key(37, ec.SECP256R1())
        shared = derive_shared_secret(watch_private, self.identity_public)
        self._auth_keys = derive_pairing_keys(shared, encoded)
        self._last_auth_envelope = self._envelope(
            CredentialPurpose.REKEY,
            transaction_id,
            self._auth_keys,
            public_key_bytes(watch_private.public_key()),
        )
        return self._last_auth_envelope

    async def begin_authentication(self, request):
        self._requested_epoch = request.agent_key_epoch
        return await self.request_authentication(request)

    def remember_pairing_envelope(self, envelope):
        self._last_auth_envelope = envelope


class PairingClientTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.identity = ec.derive_private_key(13, ec.SECP256R1())
        self.identity_public = public_key_bytes(self.identity.public_key())
        self.store = CredentialStore()
        self.watch = FakeWatch(self.identity_public, self.store)
        random_values = iter(
            (
                bytes(range(32, 48)),
                bytes(range(48, 64)),
                bytes(range(122, 138)),
            )
        )
        self.client = PairingClient(
            self.store, random_bytes=lambda size: next(random_values)
        )

    async def test_pairing_stores_credential_before_authenticated_ack(self):
        presentation = await self.client.request_pairing(
            self.watch, self.identity, "Studio Mac"
        )
        self.assertRegex(presentation.verification_code, r"^\d{6}$")
        envelope = await self.watch.confirm_pairing(
            presentation.transaction_id, True
        )

        async def fixed_confirm(transaction_id, accepted):
            self.assertTrue(accepted)
            self.watch.remember_pairing_envelope(envelope)
            return envelope

        self.watch.confirm_pairing = fixed_confirm
        result = await self.client.confirm_pairing(True)
        self.assertTrue(result.paired)
        self.assertEqual(self.store.events, ["store", "ack"])
        self.assertEqual(result.credential.key_epoch, 3)
        self.assertEqual(result.credential.master_key, self.watch.master_key)

    async def test_keychain_failure_never_sends_ack(self):
        presentation = await self.client.request_pairing(
            self.watch, self.identity, "Studio Mac"
        )
        envelope = await self.watch.confirm_pairing(
            presentation.transaction_id, True
        )
        self.watch.remember_pairing_envelope(envelope)

        async def fixed_confirm(transaction_id, accepted):
            return envelope

        self.watch.confirm_pairing = fixed_confirm
        self.store.failure = RuntimeError("keychain unavailable")
        with self.assertRaisesRegex(RuntimeError, "keychain unavailable"):
            await self.client.confirm_pairing(True)
        self.assertEqual(self.watch.acks, [])

    async def test_known_agent_authenticates_and_stale_epoch_rekeys(self):
        current = WatchCredential(
            watch_id=self.watch.watch_id,
            master_key=b"x" * 16,
            key_epoch=2,
            fingerprint=self.watch.fingerprint,
        )
        result = await self.client.authenticate(
            self.watch, self.identity, current
        )
        self.assertTrue(result.authenticated)
        self.assertTrue(result.rekeyed)
        self.assertEqual(result.credential.key_epoch, 3)
        self.assertEqual(result.credential.master_key, self.watch.master_key)
        self.assertEqual(self.store.events, ["store", "ack"])

        self.store.events.clear()
        result = await self.client.authenticate(
            self.watch, self.identity, result.credential
        )
        self.assertTrue(result.authenticated)
        self.assertFalse(result.rekeyed)
        self.assertEqual(self.store.events, [])

    async def test_tampered_wrapped_credential_is_rejected_without_store_or_ack(self):
        await self.client.request_pairing(
            self.watch, self.identity, "Studio Mac"
        )
        self.watch.tamper_envelope = True
        with self.assertRaises(Exception):
            await self.client.confirm_pairing(True)
        self.assertEqual(self.store.saved, [])
        self.assertEqual(self.watch.acks, [])


if __name__ == "__main__":
    unittest.main()
