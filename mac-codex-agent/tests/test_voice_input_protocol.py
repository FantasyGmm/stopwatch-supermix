import dataclasses
import pathlib
import sys
import unittest

from cryptography.exceptions import InvalidTag


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from voice_input_protocol import (  # noqa: E402
    BEGIN_BODY,
    AUDIO,
    ERROR_PAYLOAD,
    HEADER,
    PTT_END,
    UNAVAILABLE,
    VOICE_ERROR,
    BeginBody,
    EndSummary,
    IMA_ADPCM,
    PTT_BEGIN,
    decode_control as _decode_control,
    decode_notification as _decode_notification,
    encode_ready as _encode_ready,
    encode_unavailable as _encode_unavailable,
    pack_notification as _pack_notification,
)
from voice_input_crypto import (  # noqa: E402
    AUDIO_DOMAIN,
    PTT_BEGIN_DOMAIN,
    encrypt,
)


TEST_KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
TEST_NONCE = bytes.fromhex("1011121314151617")
TEST_EPOCH = 7
TEST_ROUTE = 0x0123456789ABCDEF


def encode_ready(key, exchange_nonce, target_bps, *, key_epoch=TEST_EPOCH):
    return _encode_ready(
        key,
        exchange_nonce,
        target_bps,
        key_epoch=key_epoch,
    )


def encode_unavailable(key, exchange_nonce, reason, *, key_epoch=TEST_EPOCH):
    return _encode_unavailable(
        key,
        exchange_nonce,
        reason,
        key_epoch=key_epoch,
    )


def decode_control(key, raw, *, expected_epoch=TEST_EPOCH):
    return _decode_control(key, raw, expected_epoch=expected_epoch)


def pack_notification(
    key,
    kind,
    session,
    sequence,
    watch_ms,
    payload,
    session_nonce,
    *,
    key_epoch=TEST_EPOCH,
):
    return _pack_notification(
        key,
        kind,
        session,
        sequence,
        watch_ms,
        payload,
        session_nonce,
        key_epoch=key_epoch,
    )


def decode_notification(
    key,
    raw,
    session_nonce=None,
    *,
    expected_epoch=TEST_EPOCH,
):
    return _decode_notification(
        key,
        raw,
        session_nonce,
        expected_epoch=expected_epoch,
    )


class VoiceInputProtocolTests(unittest.TestCase):
    def test_runtime_epoch_is_authenticated_and_must_match_exactly(self):
        ready = encode_ready(
            TEST_KEY,
            TEST_NONCE,
            8192,
            key_epoch=TEST_EPOCH,
        )
        decoded = decode_control(
            TEST_KEY,
            ready,
            expected_epoch=TEST_EPOCH,
        )
        self.assertEqual(decoded.key_epoch, TEST_EPOCH)
        with self.assertRaises(ValueError):
            decode_control(TEST_KEY, ready, expected_epoch=TEST_EPOCH + 1)

        begin = pack_notification(
            TEST_KEY,
            PTT_BEGIN,
            7,
            0,
            100,
            BEGIN_BODY.pack(IMA_ADPCM, 16000, 320, TEST_ROUTE),
            TEST_NONCE,
            key_epoch=TEST_EPOCH,
        )
        packet = decode_notification(
            TEST_KEY,
            begin,
            expected_epoch=TEST_EPOCH,
        )
        self.assertEqual(packet.key_epoch, TEST_EPOCH)
        with self.assertRaises(ValueError):
            decode_notification(
                TEST_KEY,
                begin,
                expected_epoch=TEST_EPOCH + 1,
            )

    def test_ready_control_matches_firmware_layout(self):
        self.assertEqual(
            encode_ready(TEST_KEY, TEST_NONCE, 8192),
            bytes.fromhex(
                "05010000070000001011121314151617"
                "b0f4d7ed455f4ed623909956"
            ),
        )

    def test_begin_matches_vector_and_decodes_to_immutable_logical_packet(self):
        begin_body = BeginBody(TEST_ROUTE).pack()
        raw = pack_notification(
            TEST_KEY, PTT_BEGIN, 7, 0, 100, begin_body, TEST_NONCE
        )

        self.assertEqual(
            raw,
            bytes.fromhex(
                "050107000000000064000000070000001d00"
                "1011121314151617db7a3c2d9d28e8d27667316d1f"
                "e5691b19b6fb52d2"
            ),
        )
        packet = decode_notification(TEST_KEY, raw)
        self.assertEqual(
            (
                packet.kind,
                packet.session,
                packet.sequence,
                packet.watch_ms,
                packet.session_nonce,
                packet.payload,
            ),
            (PTT_BEGIN, 7, 0, 100, TEST_NONCE, begin_body),
        )
        self.assertEqual(packet.begin, BeginBody(TEST_ROUTE))
        with self.assertRaises(dataclasses.FrozenInstanceError):
            packet.sequence = 1

    def test_begin_route_rejects_boolean_zero_and_overflow(self):
        self.assertEqual(
            BeginBody.from_payload(BeginBody(0xFFFFFFFFFFFFFFFF).pack()),
            BeginBody(0xFFFFFFFFFFFFFFFF),
        )
        for route in (True, 0, 0x10000000000000000):
            with self.subTest(route=route), self.assertRaises(ValueError):
                BeginBody(route).pack()

    def test_ready_and_unavailable_decode_as_immutable_controls(self):
        ready = decode_control(
            TEST_KEY, encode_ready(TEST_KEY, TEST_NONCE, 8192)
        )
        unavailable = decode_control(
            TEST_KEY, encode_unavailable(TEST_KEY, TEST_NONCE, reason=1)
        )

        self.assertEqual(
            (ready.kind, ready.reason, ready.exchange_nonce, ready.target_bps),
            (1, 0, TEST_NONCE, 8192),
        )
        self.assertEqual(
            (
                unavailable.kind,
                unavailable.reason,
                unavailable.exchange_nonce,
                unavailable.target_bps,
            ),
            (UNAVAILABLE, 1, TEST_NONCE, 0),
        )
        with self.assertRaises(dataclasses.FrozenInstanceError):
            ready.target_bps = 0

    def test_audio_end_and_error_round_trip_as_logical_packets(self):
        audio_payload = bytes(range(164))
        summary = EndSummary(10, 9, 1, 2, 3200, 200, 256)
        cases = (
            (AUDIO, 1, audio_payload),
            (PTT_END, 2, summary.pack()),
            (VOICE_ERROR, 3, ERROR_PAYLOAD.pack(4, 0x1234)),
        )

        for kind, sequence, payload in cases:
            with self.subTest(kind=kind):
                raw = pack_notification(
                    TEST_KEY, kind, 7, sequence, 100, payload, TEST_NONCE
                )
                packet = decode_notification(TEST_KEY, raw, TEST_NONCE)
                self.assertEqual(
                    (packet.kind, packet.session, packet.sequence, packet.payload),
                    (kind, 7, sequence, payload),
                )

        audio = pack_notification(
            TEST_KEY, AUDIO, 7, 1, 100, audio_payload, TEST_NONCE
        )
        self.assertEqual(len(audio), 190)
        self.assertEqual(EndSummary.from_payload(cases[1][2]), summary)
        with self.assertRaises(dataclasses.FrozenInstanceError):
            summary.generated_packets = 0

    def test_rejects_versions_kinds_lengths_sessions_sequences_and_codec(self):
        begin_body = BeginBody(TEST_ROUTE).pack()
        begin = pack_notification(
            TEST_KEY, PTT_BEGIN, 7, 0, 100, begin_body, TEST_NONCE
        )
        audio_payload = bytes(164)

        invalid_encodes = (
            lambda: encode_ready(TEST_KEY, TEST_NONCE, 8191),
            lambda: encode_unavailable(TEST_KEY, TEST_NONCE, 0),
            lambda: encode_unavailable(TEST_KEY, TEST_NONCE, 255),
            lambda: pack_notification(
                TEST_KEY, PTT_BEGIN, 0, 0, 100, begin_body, TEST_NONCE
            ),
            lambda: pack_notification(
                TEST_KEY, PTT_BEGIN, 7, 1, 100, begin_body, TEST_NONCE
            ),
            lambda: pack_notification(
                TEST_KEY, PTT_BEGIN, 7, 0, 100, begin_body[:-1], TEST_NONCE
            ),
            lambda: pack_notification(
                TEST_KEY,
                PTT_BEGIN,
                7,
                0,
                100,
                BEGIN_BODY.pack(IMA_ADPCM, 16000, 320, 0),
                TEST_NONCE,
            ),
            lambda: pack_notification(
                TEST_KEY, AUDIO, 7, 0, 100, audio_payload, TEST_NONCE
            ),
            lambda: pack_notification(
                TEST_KEY, AUDIO, 7, 1, 100, audio_payload[:-1], TEST_NONCE
            ),
            lambda: pack_notification(
                TEST_KEY, 99, 7, 1, 100, audio_payload, TEST_NONCE
            ),
            lambda: pack_notification(
                TEST_KEY, PTT_END, 7, 2, 100, bytes(25), TEST_NONCE
            ),
            lambda: pack_notification(
                TEST_KEY, VOICE_ERROR, 7, 2, 100, bytes(3), TEST_NONCE
            ),
            lambda: pack_notification(
                TEST_KEY, AUDIO, 7, 1, 100, audio_payload, bytes(8)
            ),
            lambda: pack_notification(
                TEST_KEY, AUDIO, 7, 0x100000000, 100, audio_payload, TEST_NONCE
            ),
        )
        for operation in invalid_encodes:
            with self.subTest(operation=operation), self.assertRaises(ValueError):
                operation()

        invalid_raw = (
            begin[: HEADER.size - 1],
            begin[:-1],
            bytes((4,)) + begin[1:],
            bytes((5, 99)) + begin[2:],
            begin[:2] + b"\x00\x00" + begin[4:],
        )
        for raw in invalid_raw:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                decode_notification(TEST_KEY, raw)

        invalid_body = BEGIN_BODY.pack(2, 16000, 320, TEST_ROUTE)
        header = HEADER.pack(5, PTT_BEGIN, 7, 0, 100, TEST_EPOCH, 29)
        aad = header + TEST_NONCE
        authenticated_invalid_begin = aad + encrypt(
            TEST_KEY,
            PTT_BEGIN_DOMAIN,
            TEST_NONCE,
            0,
            invalid_body,
            aad,
        )
        with self.assertRaises(ValueError):
            decode_notification(TEST_KEY, authenticated_invalid_begin)

        short_audio = bytes(163)
        short_audio_header = HEADER.pack(
            5,
            AUDIO,
            7,
            1,
            100,
            TEST_EPOCH,
            len(short_audio) + 8,
        )
        authenticated_short_audio = short_audio_header + encrypt(
            TEST_KEY,
            AUDIO_DOMAIN,
            TEST_NONCE,
            1,
            short_audio,
            short_audio_header,
        )
        with self.assertRaises(ValueError):
            decode_notification(
                TEST_KEY, authenticated_short_audio, TEST_NONCE
            )

        audio = pack_notification(
            TEST_KEY, AUDIO, 7, 1, 100, audio_payload, TEST_NONCE
        )
        with self.assertRaises(ValueError):
            decode_notification(TEST_KEY, audio)

    def test_rejects_ciphertext_tag_nonce_aad_header_and_key_tampering(self):
        begin = pack_notification(
            TEST_KEY,
            PTT_BEGIN,
            7,
            0,
            100,
            BeginBody(TEST_ROUTE).pack(),
            TEST_NONCE,
        )
        audio = pack_notification(
            TEST_KEY, AUDIO, 7, 1, 100, bytes(range(164)), TEST_NONCE
        )

        begin_mutations = {
            "codec ciphertext": 26,
            "route ciphertext": 31,
            "tag": len(begin) - 1,
            "clear nonce": HEADER.size,
            "header AAD": 8,
            "session": 2,
        }
        for name, index in begin_mutations.items():
            changed = bytearray(begin)
            changed[index] ^= 1
            with self.subTest(packet="begin", mutation=name), self.assertRaises(
                (InvalidTag, ValueError)
            ):
                decode_notification(TEST_KEY, bytes(changed))

        audio_mutations = {
            "ciphertext": (HEADER.size, 1),
            "tag": (len(audio) - 1, 1),
            "header AAD": (8, 1),
            "session": (2, 1),
            "sequence": (4, 2),
        }
        for name, (index, delta) in audio_mutations.items():
            changed = bytearray(audio)
            changed[index] ^= delta
            with self.subTest(packet="audio", mutation=name), self.assertRaises(
                (InvalidTag, ValueError)
            ):
                decode_notification(TEST_KEY, bytes(changed), TEST_NONCE)

        changed_nonce = bytearray(TEST_NONCE)
        changed_nonce[-1] ^= 1
        with self.assertRaises(InvalidTag):
            decode_notification(TEST_KEY, audio, bytes(changed_nonce))
        with self.assertRaises(InvalidTag):
            decode_notification(TEST_KEY, audio, bytes(reversed(TEST_NONCE)))

        ready = encode_ready(TEST_KEY, TEST_NONCE, 8192)
        for index in (0, 1, 2, 4, 12, len(ready) - 1):
            changed = bytearray(ready)
            changed[index] ^= 1
            with self.subTest(control_index=index), self.assertRaises(
                (InvalidTag, ValueError)
            ):
                decode_control(TEST_KEY, bytes(changed))


if __name__ == "__main__":
    unittest.main()
