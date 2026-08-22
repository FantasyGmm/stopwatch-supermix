import dataclasses
import pathlib
import struct
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from pet_protocol import (  # noqa: E402
    MAX_CHUNK_BYTES,
    Begin,
    Chunk,
    CommittedStatus,
    ControlReplayGuard,
    ErrorReason,
    ErrorStatus,
    MessageKind,
    PetControl,
    PetProtocolError,
    PetSource,
    PetStatus,
    ProgressStatus,
    ReplayDecision,
    StateStatus,
    StatusReplayGuard,
    Transaction,
    decode_control,
    decode_status,
    encode_begin,
    encode_cancel,
    encode_chunk,
    encode_commit,
    encode_control,
    encode_query,
    encode_restore,
    encode_status,
)
from voice_input_crypto import (  # noqa: E402
    PET_CONTROL_DOMAIN,
    PET_STATUS_DOMAIN,
    derive_nonce,
    encrypt,
)


KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
WRONG_KEY = bytes.fromhex("010102030405060708090a0b0c0d0e0f")
NONCE = bytes.fromhex("1011121314151617")
OTHER_NONCE = bytes.fromhex("2021222324252627")
TRANSACTION_ID = 0x01020304
BEGIN_HASH = bytes(range(32))
ACTIVE_HASH = bytes(range(0x20, 0x40))

QUERY_VECTOR = bytes.fromhex(
    "01010100000010111213141516175d3d462fa46f7bf1d0d92e73"
)
BEGIN_VECTOR = bytes.fromhex(
    "020102000000101112131415161788bb7e49c9531426e10fc123bb3476a5e"
    "a13598634074dda1ccef5ed97414ed5cb8d63499bd23530b0c07f3feeb23a"
    "584009d1eb13b56f25"
)
CHUNK_VECTOR = bytes.fromhex(
    "0301030000001011121314151617701326bf823da2698cb7df26e8f0ea1ca"
    "1211964d5"
)
COMMIT_VECTOR = bytes.fromhex(
    "04010400000010111213141516177cf7aeb3f23f240b4981d67c"
)
CANCEL_VECTOR = bytes.fromhex(
    "050105000000101112131415161757979d3d6216e3f6a769fcee"
)
RESTORE_VECTOR = bytes.fromhex(
    "0601060000001011121314151617e69e3b934aa0b056d3477216"
)
STATE_VECTOR = bytes.fromhex(
    "8101010000001011121314151617e9d2d655323df2382eb2570a831baa8db"
    "30b762c0779d100463f1eff70bad738e123d2a36cb55ec903c43d1d4c8da4"
    "4e7f598868b457e7d6"
)
PROGRESS_VECTOR = bytes.fromhex(
    "8201020000001011121314151617dbe2bbbd570b4401b443926883221064ff"
    "5428a1"
)
COMMITTED_VECTOR = bytes.fromhex(
    "8301030000001011121314151617ae9330e69e6cbe3aadd43061ce05c6929"
    "8a4b194842272010255eac6d361c3d1ad4dbd8f2c3a9bfe56ee2b4bf3329"
    "da4"
)
CANCELLED_VECTOR = bytes.fromhex(
    "8401040000001011121314151617d128ce47b50fd1f1092bc4c3"
)
ERROR_VECTOR = bytes.fromhex(
    "ff01050000001011121314151617528b1cf8a2cb5df7b85c0005b9d62c613"
    "19c8a559d896cc1"
)


def control(kind, payload):
    return PetControl(kind=kind, payload=payload)


def status(kind, payload):
    return PetStatus(kind=kind, payload=payload)


def authenticated_frame(kind, sequence, body, domain):
    prefix = struct.pack("<BBI8s", kind, 1, sequence, NONCE)
    return prefix + encrypt(KEY, domain, NONCE, sequence, body, prefix)


class PetProtocolVectorTests(unittest.TestCase):
    def test_control_fixed_vectors_match_firmware(self):
        self.assertEqual(
            encode_query(KEY, NONCE, 1, TRANSACTION_ID), QUERY_VECTOR
        )
        self.assertEqual(
            encode_begin(
                KEY,
                NONCE,
                2,
                transaction_id=TRANSACTION_ID,
                schema=1,
                width=144,
                height=156,
                frame_count=2,
                package_length=0x1234,
                sha256=BEGIN_HASH,
            ),
            BEGIN_VECTOR,
        )
        self.assertEqual(
            encode_chunk(
                KEY,
                NONCE,
                sequence=3,
                transaction_id=TRANSACTION_ID,
                offset=0x20,
                data=b"abc",
            ),
            CHUNK_VECTOR,
        )
        self.assertEqual(
            encode_commit(KEY, NONCE, 4, TRANSACTION_ID), COMMIT_VECTOR
        )
        self.assertEqual(
            encode_cancel(KEY, NONCE, 5, TRANSACTION_ID), CANCEL_VECTOR
        )
        self.assertEqual(
            encode_restore(KEY, NONCE, 6, 0x0A0B0C0D), RESTORE_VECTOR
        )

    def test_status_fixed_vectors_match_firmware(self):
        self.assertEqual(
            encode_status(
                KEY,
                NONCE,
                1,
                status(
                    MessageKind.STATE,
                    StateStatus(
                        TRANSACTION_ID,
                        PetSource.CUSTOM,
                        True,
                        1,
                        1,
                        7,
                        ACTIVE_HASH,
                    ),
                ),
            ),
            STATE_VECTOR,
        )
        self.assertEqual(
            encode_status(
                KEY,
                NONCE,
                2,
                status(
                    MessageKind.PROGRESS,
                    ProgressStatus(TRANSACTION_ID, 3, 0x1234),
                ),
            ),
            PROGRESS_VECTOR,
        )
        self.assertEqual(
            encode_status(
                KEY,
                NONCE,
                3,
                status(
                    MessageKind.COMMITTED,
                    CommittedStatus(TRANSACTION_ID, 8, ACTIVE_HASH),
                ),
            ),
            COMMITTED_VECTOR,
        )
        self.assertEqual(
            encode_status(
                KEY,
                NONCE,
                5,
                status(
                    MessageKind.ERROR,
                    ErrorStatus(
                        TRANSACTION_ID,
                        ErrorReason.UNSUPPORTED_SCHEMA,
                        0,
                        0,
                    ),
                ),
            ),
            ERROR_VECTOR,
        )

    def test_all_fixed_vectors_decode_to_expected_fields(self):
        self.assertEqual(decode_control(KEY, QUERY_VECTOR).query.transaction_id, TRANSACTION_ID)
        begin = decode_control(KEY, BEGIN_VECTOR).begin
        self.assertEqual(
            (
                begin.transaction_id,
                begin.schema,
                begin.width,
                begin.height,
                begin.frame_count,
                begin.package_length,
                begin.sha256,
            ),
            (TRANSACTION_ID, 1, 144, 156, 2, 0x1234, BEGIN_HASH),
        )
        chunk = decode_control(KEY, CHUNK_VECTOR).chunk
        self.assertEqual((chunk.offset, chunk.data), (0x20, b"abc"))
        self.assertEqual(decode_control(KEY, COMMIT_VECTOR).transaction.transaction_id, TRANSACTION_ID)
        self.assertEqual(decode_control(KEY, CANCEL_VECTOR).transaction.transaction_id, TRANSACTION_ID)
        self.assertEqual(decode_control(KEY, RESTORE_VECTOR).transaction.transaction_id, 0x0A0B0C0D)

        state_value = decode_status(KEY, STATE_VECTOR).state
        self.assertEqual(
            (
                state_value.source,
                state_value.usable,
                state_value.schema_min,
                state_value.schema_max,
                state_value.generation,
                state_value.active_sha256,
            ),
            (PetSource.CUSTOM, True, 1, 1, 7, ACTIVE_HASH),
        )
        progress = decode_status(KEY, PROGRESS_VECTOR).progress
        self.assertEqual((progress.acknowledged_bytes, progress.total_bytes), (3, 0x1234))
        committed = decode_status(KEY, COMMITTED_VECTOR).committed
        self.assertEqual((committed.generation, committed.active_sha256), (8, ACTIVE_HASH))
        self.assertEqual(decode_status(KEY, CANCELLED_VECTOR).cancelled.transaction_id, TRANSACTION_ID)
        error = decode_status(KEY, ERROR_VECTOR).error
        self.assertEqual(
            (error.reason, error.required_bytes, error.available_bytes),
            (ErrorReason.UNSUPPORTED_SCHEMA, 0, 0),
        )

    def test_decoded_envelopes_are_immutable_and_retain_prefix(self):
        decoded = decode_control(KEY, CHUNK_VECTOR)
        self.assertEqual((decoded.sequence, decoded.exchange_nonce), (3, NONCE))
        with self.assertRaises(dataclasses.FrozenInstanceError):
            decoded.sequence = 4


class PetProtocolAuthenticationTests(unittest.TestCase):
    def test_chunk_round_trip_and_tamper_rejection(self):
        frame = encode_chunk(
            KEY,
            NONCE,
            sequence=7,
            transaction_id=9,
            offset=20,
            data=b"abc",
        )
        self.assertEqual(decode_control(KEY, frame).chunk.data, b"abc")
        changed = frame[:-1] + bytes([frame[-1] ^ 1])
        with self.assertRaises(PetProtocolError):
            decode_control(KEY, changed)

    def test_wrong_key_domain_prefix_tag_and_length_are_rejected(self):
        malformed = []
        for index in (0, 1, 2, 6, len(CHUNK_VECTOR) - 1):
            changed = bytearray(CHUNK_VECTOR)
            changed[index] ^= 1
            malformed.append(bytes(changed))
        malformed.extend((CHUNK_VECTOR[:-1], CHUNK_VECTOR + b"\x00"))
        for frame in malformed:
            with self.subTest(frame=frame.hex()), self.assertRaises(PetProtocolError):
                decode_control(KEY, frame)
        with self.assertRaises(PetProtocolError):
            decode_control(WRONG_KEY, CHUNK_VECTOR)
        with self.assertRaises(PetProtocolError):
            decode_status(KEY, bytes([MessageKind.PROGRESS]) + CHUNK_VECTOR[1:])
        with self.assertRaises(PetProtocolError):
            decode_control(KEY, bytes([MessageKind.CHUNK]) + PROGRESS_VECTOR[1:])

    def test_zero_nonce_and_zero_or_wrapped_sequence_are_rejected(self):
        for nonce in (bytes(8), NONCE[:-1], NONCE + b"\x18"):
            with self.subTest(nonce=nonce), self.assertRaises(PetProtocolError):
                encode_query(KEY, nonce, 1, TRANSACTION_ID)
        for sequence in (0, -1, 0x100000000):
            with self.subTest(sequence=sequence), self.assertRaises(PetProtocolError):
                encode_query(KEY, NONCE, sequence, TRANSACTION_ID)
        with self.assertRaises(ValueError):
            derive_nonce(PET_CONTROL_DOMAIN, NONCE, 0)
        with self.assertRaises(ValueError):
            derive_nonce(PET_STATUS_DOMAIN, NONCE, 0)
        maximum = encode_query(KEY, NONCE, 0xFFFFFFFF, TRANSACTION_ID)
        self.assertEqual(decode_control(KEY, maximum).sequence, 0xFFFFFFFF)

        for vector in (QUERY_VECTOR, STATE_VECTOR):
            zero_nonce = bytearray(vector)
            zero_nonce[6:14] = bytes(8)
            zero_sequence = bytearray(vector)
            zero_sequence[2:6] = bytes(4)
            decoder = decode_control if vector is QUERY_VECTOR else decode_status
            for malformed in (zero_nonce, zero_sequence):
                with self.assertRaises(PetProtocolError):
                    decoder(KEY, bytes(malformed))

    def test_boolean_as_integer_and_non_integer_arguments_are_rejected(self):
        operations = (
            lambda: encode_query(KEY, NONCE, True, TRANSACTION_ID),
            lambda: encode_query(KEY, NONCE, 1, False),
            lambda: encode_query(KEY, NONCE, 1.0, TRANSACTION_ID),
            lambda: encode_begin(
                KEY, NONCE, 1, TRANSACTION_ID, False, 144, 156, 1, 20, BEGIN_HASH
            ),
            lambda: encode_chunk(KEY, NONCE, 1, TRANSACTION_ID, True, b"a"),
            lambda: encode_status(
                KEY,
                NONCE,
                1,
                status(
                    MessageKind.PROGRESS,
                    ProgressStatus(TRANSACTION_ID, False, 1),
                ),
            ),
        )
        for operation in operations:
            with self.subTest(operation=operation), self.assertRaises(PetProtocolError):
                operation()

    def test_oversized_and_malformed_control_payloads_are_rejected(self):
        with self.assertRaises(PetProtocolError):
            encode_chunk(KEY, NONCE, 1, 1, 0, b"x" * (MAX_CHUNK_BYTES + 1))
        with self.assertRaises(PetProtocolError):
            encode_chunk(KEY, NONCE, 1, 1, 0, b"")
        with self.assertRaises(PetProtocolError):
            encode_begin(KEY, NONCE, 1, 1, 1, 144, 156, 25, 20, BEGIN_HASH)
        with self.assertRaises(PetProtocolError):
            encode_begin(KEY, NONCE, 1, 1, 1, 144, 156, 1, 262145, BEGIN_HASH)
        with self.assertRaises(PetProtocolError):
            encode_begin(KEY, NONCE, 1, 1, 1, 144, 156, 1, 20, BEGIN_HASH[:-1])

        changed = authenticated_frame(
            MessageKind.CHUNK,
            3,
            struct.pack("<IIH3s", TRANSACTION_ID, 0x20, 4, b"abc"),
            PET_CONTROL_DOMAIN,
        )
        with self.assertRaises(PetProtocolError):
            decode_control(KEY, changed)

    def test_malformed_status_fields_and_unknown_reason_are_rejected(self):
        invalid_statuses = (
            status(
                MessageKind.STATE,
                StateStatus(1, 99, True, 1, 1, 0, ACTIVE_HASH),
            ),
            status(
                MessageKind.STATE,
                StateStatus(1, PetSource.CUSTOM, 1, 1, 1, 0, ACTIVE_HASH),
            ),
            status(
                MessageKind.STATE,
                StateStatus(1, PetSource.CUSTOM, True, 2, 1, 0, ACTIVE_HASH),
            ),
            status(
                MessageKind.STATE,
                StateStatus(1, PetSource.BUILT_IN, True, 1, 1, 0, ACTIVE_HASH),
            ),
            status(
                MessageKind.PROGRESS,
                ProgressStatus(1, 11, 10),
            ),
            status(
                MessageKind.ERROR,
                ErrorStatus(1, 0, 0, 0),
            ),
            status(
                MessageKind.ERROR,
                ErrorStatus(1, 15, 0, 0),
            ),
            status(
                MessageKind.ERROR,
                ErrorStatus(1, ErrorReason.UNSUPPORTED_SCHEMA, 2, 1),
            ),
            status(
                MessageKind.ERROR,
                ErrorStatus(1, ErrorReason.INSUFFICIENT_STORAGE, 10, 10),
            ),
        )
        for invalid in invalid_statuses:
            with self.subTest(invalid=invalid), self.assertRaises(PetProtocolError):
                encode_status(KEY, NONCE, 1, invalid)

        malformed_bodies = (
            struct.pack(
                "<IBBHHHI32s", TRANSACTION_ID, 2, 1, 1, 1, 0, 7, ACTIVE_HASH
            ),
            struct.pack(
                "<IBBHHHI32s", TRANSACTION_ID, 1, 2, 1, 1, 0, 7, ACTIVE_HASH
            ),
            struct.pack(
                "<IBBHHHI32s", TRANSACTION_ID, 1, 1, 1, 1, 1, 7, ACTIVE_HASH
            ),
        )
        for body in malformed_bodies:
            frame = authenticated_frame(
                MessageKind.STATE, 1, body, PET_STATUS_DOMAIN
            )
            with self.subTest(body=body.hex()), self.assertRaises(PetProtocolError):
                decode_status(KEY, frame)
        for reason, reserved in ((0, 0), (15, 0), (4, 1)):
            body = struct.pack(
                "<IHHII", TRANSACTION_ID, reason, reserved, 2, 1
            )
            frame = authenticated_frame(
                MessageKind.ERROR, 5, body, PET_STATUS_DOMAIN
            )
            with self.subTest(reason=reason, reserved=reserved), self.assertRaises(PetProtocolError):
                decode_status(KEY, frame)

    def test_unsupported_schema_is_a_closed_authenticated_error_reason(self):
        decoded = decode_status(KEY, ERROR_VECTOR).error
        self.assertEqual(decoded.reason, ErrorReason.UNSUPPORTED_SCHEMA)
        self.assertEqual((decoded.required_bytes, decoded.available_bytes), (0, 0))
        storage = status(
            MessageKind.ERROR,
            ErrorStatus(
                TRANSACTION_ID,
                ErrorReason.INSUFFICIENT_STORAGE,
                100,
                50,
            ),
        )
        self.assertEqual(
            decode_status(KEY, encode_status(KEY, NONCE, 1, storage)).error,
            storage.error,
        )
        with self.assertRaises(ValueError):
            ErrorReason(15)


class PetProtocolReplayTests(unittest.TestCase):
    def test_replay_guards_require_sequence_one_for_a_fresh_operation(self):
        control_guard = ControlReplayGuard()
        control_guard.set_connection_generation(1)
        self.assertTrue(control_guard.begin(NONCE, TRANSACTION_ID))
        skipped = encode_query(KEY, NONCE, 2, TRANSACTION_ID)
        self.assertEqual(
            control_guard.accept(decode_control(KEY, skipped), skipped),
            ReplayDecision.REJECTED,
        )

        status_guard = StatusReplayGuard()
        status_guard.set_active_nonce(NONCE)
        self.assertFalse(status_guard.accept(decode_status(KEY, PROGRESS_VECTOR)))

    def test_control_guard_accepts_increasing_chunks_and_exact_duplicate_only(self):
        guard = ControlReplayGuard()
        guard.set_connection_generation(4)
        self.assertTrue(guard.begin(NONCE, TRANSACTION_ID))
        first = encode_chunk(KEY, NONCE, 1, TRANSACTION_ID, 0, b"abc")
        first_control = decode_control(KEY, first)
        self.assertEqual(guard.accept(first_control, first), ReplayDecision.ACCEPTED)
        self.assertEqual(guard.accept(first_control, first), ReplayDecision.DUPLICATE)

        conflicting = encode_chunk(KEY, NONCE, 1, TRANSACTION_ID, 0, b"abd")
        self.assertEqual(
            guard.accept(decode_control(KEY, conflicting), conflicting),
            ReplayDecision.REJECTED,
        )
        gap = encode_chunk(KEY, NONCE, 2, TRANSACTION_ID, 4, b"x")
        self.assertEqual(
            guard.accept(decode_control(KEY, gap), gap), ReplayDecision.REJECTED
        )
        second = encode_chunk(KEY, NONCE, 2, TRANSACTION_ID, 3, b"x")
        self.assertEqual(
            guard.accept(decode_control(KEY, second), second),
            ReplayDecision.ACCEPTED,
        )

    def test_control_guard_rejects_stale_sequence_nonce_and_transaction(self):
        guard = ControlReplayGuard()
        guard.set_connection_generation(1)
        self.assertTrue(guard.begin(NONCE, TRANSACTION_ID))
        frame = encode_query(KEY, NONCE, 1, TRANSACTION_ID)
        self.assertEqual(
            guard.accept(decode_control(KEY, frame), frame),
            ReplayDecision.ACCEPTED,
        )
        self.assertEqual(
            guard.accept(decode_control(KEY, frame), frame),
            ReplayDecision.REJECTED,
        )
        wrong_nonce = encode_commit(KEY, OTHER_NONCE, 2, TRANSACTION_ID)
        self.assertEqual(
            guard.accept(decode_control(KEY, wrong_nonce), wrong_nonce),
            ReplayDecision.REJECTED,
        )
        wrong_transaction = encode_commit(KEY, NONCE, 2, TRANSACTION_ID + 1)
        self.assertEqual(
            guard.accept(decode_control(KEY, wrong_transaction), wrong_transaction),
            ReplayDecision.REJECTED,
        )

    def test_completed_transaction_tombstones_are_bounded_and_generation_scoped(self):
        guard = ControlReplayGuard()
        guard.set_connection_generation(9)
        for transaction_id in range(1, 10):
            nonce = transaction_id.to_bytes(8, "little")
            self.assertTrue(guard.begin(nonce, transaction_id))
            guard.retire(completed=True)
        self.assertFalse(guard.begin((9).to_bytes(8, "little"), 9))
        self.assertTrue(guard.begin((1).to_bytes(8, "little"), 1))
        guard.retire(completed=True)
        guard.set_connection_generation(10)
        self.assertTrue(guard.begin((9).to_bytes(8, "little"), 9))

        fresh = ControlReplayGuard()
        fresh.set_connection_generation(1)
        self.assertTrue(fresh.begin(NONCE, 1))
        fresh.retire(completed=True)
        self.assertFalse(fresh.begin(NONCE, 2))

    def test_status_guard_requires_active_nonce_and_strictly_increasing_sequences(self):
        guard = StatusReplayGuard()
        state = decode_status(KEY, STATE_VECTOR)
        progress = decode_status(KEY, PROGRESS_VECTOR)
        self.assertFalse(guard.accept(state))
        guard.set_active_nonce(NONCE)
        self.assertTrue(guard.accept(state))
        self.assertFalse(guard.accept(state))
        self.assertTrue(guard.accept(progress))
        guard.retire()
        self.assertFalse(guard.accept(progress))


if __name__ == "__main__":
    unittest.main()
