import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from pairing_client import (  # noqa: E402
    AuthenticationChallenge,
    AuthenticationRequest,
    BeginPairingRequest,
    CredentialEnvelope,
    CredentialPurpose,
    PairingOffer,
)
from secure_pairing_wire import (  # noqa: E402
    PairingWireError,
    WireMessageType,
    decode_authentication_challenge,
    decode_authentication_request,
    decode_credential_envelope,
    decode_frame,
    decode_pairing_offer,
    decode_pairing_request,
    encode_authentication_challenge,
    encode_authentication_request,
    encode_credential_envelope,
    encode_frame,
    encode_pairing_offer,
    encode_pairing_request,
)


class PairingWireTests(unittest.TestCase):
    def test_pairing_request_round_trips_utf8_name(self):
        request = BeginPairingRequest(
            transaction_id=7,
            agent_nonce=b"N" * 16,
            agent_ephemeral_public=b"\x02" + b"E" * 32,
            agent_identity_public=b"\x03" + b"I" * 32,
            display_name="桌面 Agent",
        )

        frame = encode_pairing_request(request)
        kind, transaction_id, payload = decode_frame(frame)

        self.assertIs(kind, WireMessageType.PAIRING_REQUEST)
        self.assertEqual(transaction_id, 7)
        self.assertEqual(decode_pairing_request(transaction_id, payload), request)
        self.assertLessEqual(len(frame), 256)

    def test_pairing_offer_round_trips(self):
        offer = PairingOffer(
            transaction_id=8,
            watch_id=b"W" * 16,
            watch_nonce=b"R" * 16,
            watch_ephemeral_public=b"\x02" + b"P" * 32,
        )

        kind, transaction_id, payload = decode_frame(encode_pairing_offer(offer))

        self.assertIs(kind, WireMessageType.PAIRING_OFFER)
        self.assertEqual(decode_pairing_offer(transaction_id, payload), offer)

    def test_authentication_request_and_challenge_round_trip(self):
        request = AuthenticationRequest(9, b"F" * 16, 2, "Studio Mac")
        challenge = AuthenticationChallenge(
            9, b"W" * 16, b"C" * 16, 5, 1, "Studio Mac"
        )

        request_frame = decode_frame(encode_authentication_request(request))
        challenge_frame = decode_frame(
            encode_authentication_challenge(challenge)
        )

        self.assertEqual(
            decode_authentication_request(request_frame[1], request_frame[2]),
            request,
        )
        self.assertEqual(
            decode_authentication_challenge(
                challenge_frame[1], challenge_frame[2]
            ),
            challenge,
        )

    def test_credential_envelope_round_trips_without_logging_key_plaintext(self):
        envelope = CredentialEnvelope(
            purpose=CredentialPurpose.REKEY,
            transaction_id=10,
            key_epoch=6,
            watch_ephemeral_public=b"\x03" + b"P" * 32,
            nonce=b"N" * 13,
            ciphertext=b"ciphertext-and-tag",
            ack_nonce=b"A" * 13,
        )

        kind, transaction_id, payload = decode_frame(
            encode_credential_envelope(envelope)
        )

        self.assertIs(kind, WireMessageType.CREDENTIAL)
        self.assertEqual(
            decode_credential_envelope(transaction_id, payload), envelope
        )

    def test_bad_magic_length_and_oversize_are_rejected(self):
        frame = encode_frame(WireMessageType.DISCOVERY_REQUEST, 1, b"")
        cases = (
            b"BAD!" + frame[4:],
            frame[:-1],
        )
        for candidate in cases:
            with self.subTest(size=len(candidate)):
                with self.assertRaises((PairingWireError, ValueError)):
                    decode_frame(candidate)
        with self.assertRaises(PairingWireError):
            encode_frame(WireMessageType.ERROR, 1, b"x" * 245)


if __name__ == "__main__":
    unittest.main()
