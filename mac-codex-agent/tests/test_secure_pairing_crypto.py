import dataclasses
import unittest

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.asymmetric import ec

from secure_pairing_crypto import (
    derive_pairing_keys,
    derive_shared_secret,
    public_key_bytes,
    sign_challenge,
    unwrap_credential,
    verification_code,
    verify_challenge,
    wrap_credential,
)
from secure_pairing_protocol import (
    PairingIdentityClaim,
    PairingTranscript,
    decode_pairing_transcript,
    encode_pairing_identity_claim,
    encode_pairing_transcript,
)


WATCH_PRIVATE_SCALAR = int("11" * 32, 16)
AGENT_PRIVATE_SCALAR = int("22" * 32, 16)
IDENTITY_PRIVATE_SCALAR = int("33" * 32, 16)

WATCH_PUBLIC_HEX = (
    "020217e617f0b6443928278f96999e69"
    "a23a4f2c152bdf6d6cdf66e5b80282d4ed"
)
AGENT_PUBLIC_HEX = (
    "03d65a93977caa3d1b081852ff57a79e"
    "465f1660577304baead505dd3a48589cf3"
)
IDENTITY_PUBLIC_HEX = (
    "0351a7580833898ea1b183cbd7350a40"
    "99078c6ef1c1e18e970cd7683035f25e7d"
)
EXPECTED_TRANSCRIPT_HEX = (
    "5357504149520101020304"
    "0010d4c3b2a19080706050403020100f0e0d"
    "001000112233445566778899aabbccddeeff"
    "0010ffeeddccbbaa99887766554433221100"
    "0021" + WATCH_PUBLIC_HEX +
    "0021" + IDENTITY_PUBLIC_HEX +
    "0021" + AGENT_PUBLIC_HEX +
    "0021" + IDENTITY_PUBLIC_HEX +
    "000a53747564696f204d6163"
)
EXPECTED_IDENTITY_CLAIM_HEX = (
    "535750434c4d0101020304"
    "0010ffeeddccbbaa99887766554433221100"
    "0021" + AGENT_PUBLIC_HEX +
    "0021" + IDENTITY_PUBLIC_HEX +
    "000a53747564696f204d6163"
)
EXPECTED_SHARED_SECRET_HEX = (
    "ccfc261f58193c98ca4ad4a53bbac6f0"
    "ee29bc4d48438090446908622ca79af6"
)
EXPECTED_CONFIRM_KEY_HEX = "b8a80f8e6163543c9f246f966de93072"
EXPECTED_WRAP_KEY_HEX = "9f0df5d9315a44cb61c4fc5f0e23a79e"
EXPECTED_ACK_KEY_HEX = "5887abf88287da67131dfd25085e9b18"
WRAP_NONCE = bytes.fromhex("0102030405060708090a0b0c0d")
CREDENTIAL = bytes.fromhex(
    "00112233445566778899aabbccddeeff"
    "00000007"
    "d4c3b2a19080706050403020100f0e0d"
    "2ea06a7bca900cd2410d78584bdc1add"
)
EXPECTED_WRAPPED_HEX = (
    "a932792ba08de46917aa1c7469d8fd51"
    "c1d7d45a785b81d33b381be7fbee8395"
    "2a01d6472584e8f619ce5531c0baf8ed"
    "583758f77d639f494e29bb71"
)


def fixture() -> PairingTranscript:
    return PairingTranscript(
        transaction_id=0x01020304,
        watch_id=bytes.fromhex("d4c3b2a19080706050403020100f0e0d"),
        watch_nonce=bytes.fromhex("00112233445566778899aabbccddeeff"),
        agent_nonce=bytes.fromhex("ffeeddccbbaa99887766554433221100"),
        watch_ephemeral_public=bytes.fromhex(WATCH_PUBLIC_HEX),
        watch_identity_public=bytes.fromhex(IDENTITY_PUBLIC_HEX),
        agent_ephemeral_public=bytes.fromhex(AGENT_PUBLIC_HEX),
        agent_identity_public=bytes.fromhex(IDENTITY_PUBLIC_HEX),
        agent_name="Studio Mac",
    )


class SecurePairingCryptoTests(unittest.TestCase):
    def test_identity_claim_vector_is_byte_identical(self):
        claim = PairingIdentityClaim(
            transaction_id=0x01020304,
            agent_nonce=bytes.fromhex("ffeeddccbbaa99887766554433221100"),
            agent_ephemeral_public=bytes.fromhex(AGENT_PUBLIC_HEX),
            agent_identity_public=bytes.fromhex(IDENTITY_PUBLIC_HEX),
            agent_name="Studio Mac",
        )
        self.assertEqual(
            encode_pairing_identity_claim(claim).hex(),
            EXPECTED_IDENTITY_CLAIM_HEX,
        )

    def test_pairing_vector_is_byte_identical(self):
        transcript = encode_pairing_transcript(fixture())
        self.assertEqual(transcript.hex(), EXPECTED_TRANSCRIPT_HEX)
        self.assertEqual(decode_pairing_transcript(transcript), fixture())

        watch_private = ec.derive_private_key(
            WATCH_PRIVATE_SCALAR, ec.SECP256R1()
        )
        agent_private = ec.derive_private_key(
            AGENT_PRIVATE_SCALAR, ec.SECP256R1()
        )
        self.assertEqual(public_key_bytes(watch_private.public_key()).hex(), WATCH_PUBLIC_HEX)
        self.assertEqual(public_key_bytes(agent_private.public_key()).hex(), AGENT_PUBLIC_HEX)

        shared_secret = derive_shared_secret(
            watch_private, bytes.fromhex(AGENT_PUBLIC_HEX)
        )
        self.assertEqual(shared_secret.hex(), EXPECTED_SHARED_SECRET_HEX)
        self.assertEqual(
            derive_shared_secret(agent_private, bytes.fromhex(WATCH_PUBLIC_HEX)),
            shared_secret,
        )

        keys = derive_pairing_keys(shared_secret, transcript)
        self.assertEqual(keys.confirm_key.hex(), EXPECTED_CONFIRM_KEY_HEX)
        self.assertEqual(keys.wrap_key.hex(), EXPECTED_WRAP_KEY_HEX)
        self.assertEqual(keys.ack_key.hex(), EXPECTED_ACK_KEY_HEX)
        self.assertEqual(verification_code(keys.confirm_key, transcript), "782441")

        wrapped = wrap_credential(keys.wrap_key, WRAP_NONCE, CREDENTIAL, transcript)
        self.assertEqual(wrapped.hex(), EXPECTED_WRAPPED_HEX)
        self.assertEqual(
            unwrap_credential(keys.wrap_key, WRAP_NONCE, wrapped, transcript),
            CREDENTIAL,
        )

    def test_every_transcript_field_is_bound(self):
        original = fixture()
        original_bytes = encode_pairing_transcript(original)
        confirm_key = bytes.fromhex(EXPECTED_CONFIRM_KEY_HEX)
        original_code = verification_code(confirm_key, original_bytes)
        changes = {
            "transaction_id": original.transaction_id + 1,
            "watch_id": b"x" + original.watch_id[1:],
            "watch_nonce": b"x" + original.watch_nonce[1:],
            "agent_nonce": b"x" + original.agent_nonce[1:],
            "watch_ephemeral_public": bytes.fromhex(AGENT_PUBLIC_HEX),
            "watch_identity_public": bytes.fromhex(WATCH_PUBLIC_HEX),
            "agent_ephemeral_public": bytes.fromhex(WATCH_PUBLIC_HEX),
            "agent_identity_public": bytes.fromhex(WATCH_PUBLIC_HEX),
            "agent_name": "Studio Mac 2",
        }

        for field, value in changes.items():
            with self.subTest(field=field):
                changed = encode_pairing_transcript(
                    dataclasses.replace(original, **{field: value})
                )
                self.assertNotEqual(changed, original_bytes)
                self.assertNotEqual(
                    verification_code(confirm_key, changed), original_code
                )

    def test_signature_and_wrapping_reject_tampering(self):
        transcript = encode_pairing_transcript(fixture())
        identity_private = ec.derive_private_key(
            IDENTITY_PRIVATE_SCALAR, ec.SECP256R1()
        )
        signature = sign_challenge(identity_private, transcript)
        self.assertTrue(
            verify_challenge(
                bytes.fromhex(IDENTITY_PUBLIC_HEX), transcript, signature
            )
        )
        self.assertFalse(
            verify_challenge(
                bytes.fromhex(IDENTITY_PUBLIC_HEX),
                transcript[:-1] + b"x",
                signature,
            )
        )

        wrap_key = bytes.fromhex(EXPECTED_WRAP_KEY_HEX)
        wrapped = wrap_credential(wrap_key, WRAP_NONCE, CREDENTIAL, transcript)
        with self.assertRaises(InvalidTag):
            unwrap_credential(
                wrap_key,
                WRAP_NONCE,
                wrapped,
                transcript[:-1] + b"x",
            )

    def test_decoder_rejects_noncanonical_or_unsafe_transcripts(self):
        encoded = encode_pairing_transcript(fixture())
        zero_watch_nonce = bytearray(encoded)
        zero_watch_nonce[31:47] = b"\x00" * 16
        malformed = (
            encoded + b"\x00",
            encoded[:6] + b"\x02" + encoded[7:],
            bytes(zero_watch_nonce),
        )
        for value in malformed:
            with self.subTest(value=value.hex()[-16:]):
                with self.assertRaises(ValueError):
                    decode_pairing_transcript(value)

        invalid_names = ("", "x" * 25, "bad\nname", "e\u0301")
        for name in invalid_names:
            with self.subTest(name=repr(name)):
                with self.assertRaises(ValueError):
                    encode_pairing_transcript(
                        dataclasses.replace(fixture(), agent_name=name)
                    )


if __name__ == "__main__":
    unittest.main()
