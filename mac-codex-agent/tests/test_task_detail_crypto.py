import json
import pathlib
import sys
import unittest

from cryptography.exceptions import InvalidTag


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from task_detail_crypto import (  # noqa: E402
    canonical_task_detail_aad,
    decrypt_task_detail,
    encrypt_task_detail,
)
from voice_input_crypto import TASK_DETAIL_DOMAIN, derive_nonce  # noqa: E402


TEST_KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
TEST_NONCE = bytes.fromhex("1011121314151617")


class TaskDetailCryptoTests(unittest.TestCase):
    def test_fixed_wire_vector_locks_domain_nonce_aad_and_ciphertext(self):
        sequence = 0x01020304
        aad = canonical_task_detail_aad(
            message_type="task_detail",
            version=1,
            key_epoch=7,
            exchange_nonce=TEST_NONCE,
            sequence=sequence,
            route="0123456789abcdef",
            revision=12,
            index=1,
            count=3,
        )
        envelope = encrypt_task_detail(
            TEST_KEY,
            key_epoch=7,
            exchange_nonce=TEST_NONCE,
            sequence=sequence,
            route="0123456789abcdef",
            revision=12,
            index=1,
            count=3,
            text="fixed 私密",
        )

        self.assertEqual(TASK_DETAIL_DOMAIN, 0x24)
        self.assertEqual(
            derive_nonce(TASK_DETAIL_DOMAIN, TEST_NONCE, sequence).hex(),
            "24101112131415161704030201",
        )
        self.assertEqual(
            aad,
            b'{"type":"task_detail","version":1,'
            b'"route":"0123456789abcdef","revision":12,'
            b'"index":1,"count":3,"epoch":7,'
            b'"nonce":"1011121314151617","sequence":16909060}',
        )
        self.assertEqual(envelope["ciphertext"], "MgJpSHfh+JIsO/mNgxI4BI09QBA=")

    def test_canonical_aad_rejects_version_mutation_and_omission(self):
        arguments = {
            "message_type": "task_detail",
            "version": 1,
            "key_epoch": 7,
            "exchange_nonce": TEST_NONCE,
            "sequence": 9,
            "route": "0123456789abcdef",
            "revision": 12,
            "index": 1,
            "count": 3,
        }

        with self.assertRaises(ValueError):
            canonical_task_detail_aad(**dict(arguments, version=2))
        without_version = dict(arguments)
        without_version.pop("version")
        with self.assertRaises(TypeError):
            canonical_task_detail_aad(**without_version)

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
            {"sequence": 0},
            {"route": "0000000000000000"},
            {"route": "INVALID"},
            {"revision": -1},
            {"revision": 0x80000000},
            {"index": 1, "count": 1},
            {"count": 17},
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
