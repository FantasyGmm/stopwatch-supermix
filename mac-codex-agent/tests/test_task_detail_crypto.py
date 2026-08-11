import json
import pathlib
import sys
import unittest

from cryptography.exceptions import InvalidTag


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from task_detail_crypto import decrypt_task_detail, encrypt_task_detail  # noqa: E402


TEST_KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
TEST_NONCE = bytes.fromhex("1011121314151617")


class TaskDetailCryptoTests(unittest.TestCase):
    def test_round_trip_encrypts_text_and_exposes_only_authenticated_metadata(self):
        envelope = encrypt_task_detail(
            TEST_KEY,
            key_epoch=7,
            exchange_nonce=TEST_NONCE,
            sequence=9,
            route="0123456789abcdef",
            revision=12,
            index=1,
            count=3,
            text="exact private reply 私密",
        )

        self.assertEqual(
            set(envelope),
            {
                "type",
                "route",
                "revision",
                "index",
                "count",
                "epoch",
                "nonce",
                "sequence",
                "ciphertext",
            },
        )
        self.assertEqual(envelope["type"], "task_detail")
        self.assertEqual(envelope["nonce"], TEST_NONCE.hex())
        self.assertNotIn("exact private reply", json.dumps(envelope))
        self.assertEqual(
            decrypt_task_detail(TEST_KEY, envelope),
            "exact private reply 私密",
        )

    def test_canonical_aad_authenticates_every_exposed_metadata_field(self):
        envelope = encrypt_task_detail(
            TEST_KEY,
            key_epoch=7,
            exchange_nonce=TEST_NONCE,
            sequence=9,
            route="0123456789abcdef",
            revision=12,
            index=1,
            count=3,
            text="secret",
        )
        mutations = {
            "type": "task_detail_v2",
            "route": "fedcba9876543210",
            "revision": 13,
            "index": 2,
            "count": 4,
            "epoch": 8,
            "nonce": "2011121314151617",
            "sequence": 10,
        }

        for field, value in mutations.items():
            with self.subTest(field=field), self.assertRaises(
                (InvalidTag, ValueError)
            ):
                decrypt_task_detail(TEST_KEY, dict(envelope, **{field: value}))

    def test_rejects_invalid_metadata_and_non_utf8_text(self):
        cases = (
            {"key_epoch": True},
            {"key_epoch": 0},
            {"exchange_nonce": bytes(8)},
            {"sequence": 0x100000000},
            {"route": "INVALID"},
            {"revision": -1},
            {"revision": 0x80000000},
            {"index": 1, "count": 1},
            {"text": "bad\ud800"},
        )
        defaults = {
            "key_epoch": 7,
            "exchange_nonce": TEST_NONCE,
            "sequence": 9,
            "route": "0123456789abcdef",
            "revision": 12,
            "index": 0,
            "count": 1,
            "text": "secret",
        }

        for changes in cases:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                encrypt_task_detail(TEST_KEY, **dict(defaults, **changes))


if __name__ == "__main__":
    unittest.main()
