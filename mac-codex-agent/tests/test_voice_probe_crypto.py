import pathlib
import sys
import unittest

from cryptography.exceptions import InvalidTag


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from voice_probe_crypto import (  # noqa: E402
    CIPHERTEXT_BYTES,
    PLAINTEXT_BYTES,
    decrypt,
    decrypt_synthetic,
    derive_nonce,
    encrypt,
    encrypt_synthetic,
)


KEY_VECTOR_NONCE = bytes.fromhex("101112131415161718191a1b1c")
AAD = bytes.fromhex("020207000100000064000000ac00")
PLAINTEXT = bytes(PLAINTEXT_BYTES)
EXPECTED_CIPHERTEXT = bytes.fromhex(
    "7ce17041b85ceed4bb5948912f076c812ad355797f979e2a3d4d1ad38eb885430"
    "57a9fde2190a49a3f6ca33e9f7b96618c9ce5c51bb7600d9fe2d79627c3f70f5"
    "c917ad213e94d2ac87c80d85b1770ad8b0d56b6e1cfd63186c90a740972c30e1"
    "f964ef8857931bae5f4d3b9e01eb7fc4437636d71976153bdb1c63f16c8fd5c5"
    "3720cdfa86184af620540f33fe593b122581362678afe9a5c27fd20cafc3fd03a"
    "3a2f12f2ba40f8e73af636"
)


class VoiceProbeCryptoTests(unittest.TestCase):
    def test_fixed_vector_matches_and_round_trips(self):
        self.assertEqual(len(EXPECTED_CIPHERTEXT), CIPHERTEXT_BYTES)
        ciphertext = encrypt(AAD, PLAINTEXT, KEY_VECTOR_NONCE)
        self.assertEqual(ciphertext, EXPECTED_CIPHERTEXT)
        self.assertEqual(decrypt(AAD, ciphertext, KEY_VECTOR_NONCE), PLAINTEXT)

    def test_nonce_layout_and_zero_session_rejection(self):
        self.assertEqual(
            derive_nonce(0x1413, 0x18171615, 0x1C1B1A19),
            KEY_VECTOR_NONCE,
        )
        with self.assertRaises(ValueError):
            derive_nonce(0, 1, 1)

    def test_rejects_tampered_ciphertext_tag_nonce_and_aad(self):
        mutations = []
        for index, name in (
            (0, "ciphertext"),
            (CIPHERTEXT_BYTES - 1, "tag"),
        ):
            changed = bytearray(EXPECTED_CIPHERTEXT)
            changed[index] ^= 0x01
            mutations.append((name, AAD, bytes(changed), KEY_VECTOR_NONCE))

        changed_nonce = bytearray(KEY_VECTOR_NONCE)
        changed_nonce[-1] ^= 0x01
        mutations.append(("nonce", AAD, EXPECTED_CIPHERTEXT, bytes(changed_nonce)))

        changed_aad = bytearray(AAD)
        changed_aad[-1] ^= 0x01
        mutations.append(("aad", bytes(changed_aad), EXPECTED_CIPHERTEXT, KEY_VECTOR_NONCE))

        for name, aad, ciphertext, nonce in mutations:
            with self.subTest(name=name), self.assertRaises(InvalidTag):
                decrypt(aad, ciphertext, nonce)

    def test_rejects_zero_nonce_and_wrong_lengths(self):
        with self.assertRaises(ValueError):
            encrypt(AAD, bytes(PLAINTEXT_BYTES - 1), KEY_VECTOR_NONCE)
        with self.assertRaises(ValueError):
            encrypt(AAD, PLAINTEXT, bytes(13))
        with self.assertRaises(ValueError):
            decrypt(AAD, EXPECTED_CIPHERTEXT[:-1], KEY_VECTOR_NONCE)

    def test_variable_synthetic_payload_round_trips_at_mtu_256_limit(self):
        plaintext = bytes((index + 7) & 0xFF for index in range(231))
        aad = bytes.fromhex("020207000100000064000000ef00")
        ciphertext = encrypt_synthetic(aad, plaintext, KEY_VECTOR_NONCE)

        self.assertEqual(len(aad) + len(ciphertext), 253)
        self.assertEqual(
            decrypt_synthetic(aad, ciphertext, KEY_VECTOR_NONCE),
            plaintext,
        )

        tampered = bytearray(ciphertext)
        tampered[-1] ^= 0x01
        with self.assertRaises(InvalidTag):
            decrypt_synthetic(aad, bytes(tampered), KEY_VECTOR_NONCE)


if __name__ == "__main__":
    unittest.main()
