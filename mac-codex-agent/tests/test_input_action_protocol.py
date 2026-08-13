import dataclasses
import pathlib
import sys
import unittest

from cryptography.exceptions import InvalidTag


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from input_action_protocol import (  # noqa: E402
    ACTION_HEADER,
    CONTROL_AAD,
    ActionPacket,
    ActionReplayGuard,
    InputAction,
    decode_action,
    decode_ready,
    encode_action,
    encode_ready,
)


TEST_KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
TEST_NONCE = bytes.fromhex("1011121314151617")
READY_VECTOR = bytes.fromhex(
    "010100001011121314151617fc73d242a022667e"
)
SUBMIT_VECTOR = bytes.fromhex(
    "0101010000000102030408009085a9571d1cff3c"
)
FORCE_STEER_VECTOR = bytes.fromhex(
    "0102ffffffffffffffff08007b4211635bc8f5ca"
)


class InputActionProtocolTests(unittest.TestCase):
    def test_fixed_vectors_match_firmware(self):
        self.assertEqual(encode_ready(TEST_KEY, TEST_NONCE), READY_VECTOR)
        self.assertEqual(
            encode_action(
                TEST_KEY,
                InputAction.SUBMIT,
                1,
                0x04030201,
                TEST_NONCE,
            ),
            SUBMIT_VECTOR,
        )
        self.assertEqual(
            encode_action(
                TEST_KEY,
                InputAction.FORCE_STEER,
                0xFFFFFFFF,
                0xFFFFFFFF,
                TEST_NONCE,
            ),
            FORCE_STEER_VECTOR,
        )

    def test_ready_and_actions_round_trip(self):
        self.assertEqual(decode_ready(TEST_KEY, READY_VECTOR), TEST_NONCE)
        submit = decode_action(TEST_KEY, SUBMIT_VECTOR, TEST_NONCE)
        force_steer = decode_action(TEST_KEY, FORCE_STEER_VECTOR, TEST_NONCE)
        self.assertEqual(
            submit,
            ActionPacket(InputAction.SUBMIT, 1, 0x04030201),
        )
        self.assertEqual(
            force_steer,
            ActionPacket(InputAction.FORCE_STEER, 0xFFFFFFFF, 0xFFFFFFFF),
        )
        with self.assertRaises(dataclasses.FrozenInstanceError):
            submit.sequence = 2

    def test_encode_rejects_invalid_nonce_action_sequence_and_watch_time(self):
        invalid_operations = (
            lambda: encode_ready(TEST_KEY, bytes(8)),
            lambda: encode_ready(TEST_KEY, TEST_NONCE[:-1]),
            lambda: encode_action(TEST_KEY, 99, 1, 0, TEST_NONCE),
            lambda: encode_action(
                TEST_KEY, InputAction.SUBMIT, 0, 0, TEST_NONCE
            ),
            lambda: encode_action(
                TEST_KEY, InputAction.SUBMIT, 0x100000000, 0, TEST_NONCE
            ),
            lambda: encode_action(
                TEST_KEY, InputAction.SUBMIT, 1, -1, TEST_NONCE
            ),
            lambda: encode_action(
                TEST_KEY, InputAction.SUBMIT, 1, 0x100000000, TEST_NONCE
            ),
            lambda: encode_action(
                TEST_KEY, InputAction.SUBMIT, 1, 0, bytes(8)
            ),
        )
        for operation in invalid_operations:
            with self.subTest(operation=operation), self.assertRaises(ValueError):
                operation()

    def test_encode_rejects_boolean_and_non_integer_numeric_fields(self):
        invalid_operations = (
            lambda: encode_action(
                TEST_KEY, InputAction.SUBMIT, True, 0, TEST_NONCE
            ),
            lambda: encode_action(
                TEST_KEY, InputAction.SUBMIT, 1.0, 0, TEST_NONCE
            ),
            lambda: encode_action(
                TEST_KEY, InputAction.SUBMIT, 1, False, TEST_NONCE
            ),
            lambda: encode_action(
                TEST_KEY, InputAction.SUBMIT, 1, 1.0, TEST_NONCE
            ),
        )
        for operation in invalid_operations:
            with self.subTest(operation=operation), self.assertRaises(ValueError):
                operation()

    def test_ready_rejects_wrong_version_kind_reserved_length_tag_and_nonce(self):
        mutations = {
            "version": (0, 1),
            "kind": (1, 1),
            "reserved low": (2, 1),
            "reserved high": (3, 1),
            "nonce": (4, 1),
            "tag": (len(READY_VECTOR) - 1, 1),
        }
        for name, (index, delta) in mutations.items():
            changed = bytearray(READY_VECTOR)
            changed[index] ^= delta
            with self.subTest(name=name), self.assertRaises(
                (InvalidTag, ValueError)
            ):
                decode_ready(TEST_KEY, bytes(changed))

        zero_nonce = bytearray(READY_VECTOR)
        zero_nonce[4:12] = bytes(8)
        malformed = (READY_VECTOR[:-1], READY_VECTOR + b"\x00", bytes(zero_nonce))
        for raw in malformed:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                decode_ready(TEST_KEY, raw)

    def test_action_rejects_malformed_or_unauthenticated_frames(self):
        mutations = {
            "version": (0, 1),
            "kind": (1, 0x7F),
            "sequence": (2, 1),
            "watch_ms": (6, 1),
            "body size": (10, 1),
            "tag": (len(SUBMIT_VECTOR) - 1, 1),
        }
        for name, (index, delta) in mutations.items():
            changed = bytearray(SUBMIT_VECTOR)
            changed[index] ^= delta
            with self.subTest(name=name), self.assertRaises(
                (InvalidTag, ValueError)
            ):
                decode_action(TEST_KEY, bytes(changed), TEST_NONCE)

        zero_sequence = bytearray(SUBMIT_VECTOR)
        zero_sequence[2:6] = bytes(4)
        wrong_nonce = bytearray(TEST_NONCE)
        wrong_nonce[-1] ^= 1
        malformed = (
            SUBMIT_VECTOR[:-1],
            SUBMIT_VECTOR + b"\x00",
            bytes(zero_sequence),
        )
        for raw in malformed:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                decode_action(TEST_KEY, raw, TEST_NONCE)
        with self.assertRaises(InvalidTag):
            decode_action(TEST_KEY, SUBMIT_VECTOR, bytes(wrong_nonce))
        with self.assertRaises(ValueError):
            decode_action(TEST_KEY, SUBMIT_VECTOR, bytes(8))

    def test_replay_guard_requires_active_nonce_and_strictly_increasing_sequence(self):
        guard = ActionReplayGuard()
        wrong_nonce = bytes.fromhex("2021222324252627")

        self.assertFalse(guard.accept(TEST_NONCE, 1))
        guard.set_active_nonce(TEST_NONCE)
        self.assertTrue(guard.accept(TEST_NONCE, 1))
        self.assertFalse(guard.accept(TEST_NONCE, 1))
        self.assertFalse(guard.accept(TEST_NONCE, 0))
        self.assertFalse(guard.accept(TEST_NONCE, 1))
        self.assertTrue(guard.accept(TEST_NONCE, 2))
        self.assertFalse(guard.accept(wrong_nonce, 5))
        self.assertTrue(guard.accept(TEST_NONCE, 5))

        guard.set_active_nonce(TEST_NONCE)
        self.assertFalse(guard.accept(TEST_NONCE, 1))
        guard.set_active_nonce(wrong_nonce)
        self.assertTrue(guard.accept(wrong_nonce, 1))
        self.assertFalse(guard.accept(TEST_NONCE, 2))

        with self.assertRaises(ValueError):
            guard.set_active_nonce(bytes(8))

    def test_replay_guard_rejects_boolean_and_non_integer_sequences(self):
        guard = ActionReplayGuard()
        guard.set_active_nonce(TEST_NONCE)

        for sequence in (True, False, 1.0):
            with self.subTest(sequence=sequence), self.assertRaises(ValueError):
                guard.accept(TEST_NONCE, sequence)


if __name__ == "__main__":
    unittest.main()
