import pathlib
import subprocess
import sys
import unittest

from cryptography.exceptions import InvalidTag


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from voice_input_crypto import (  # noqa: E402
    AUDIO_DOMAIN,
    PTT_BEGIN_DOMAIN,
    PTT_END_DOMAIN,
    READY_DOMAIN,
    UNAVAILABLE_DOMAIN,
    VOICE_ERROR_DOMAIN,
    VoiceKeyError,
    decrypt,
    derive_nonce,
    encrypt,
    load_key_from_keychain,
)


TEST_KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
TEST_NONCE = bytes.fromhex("1011121314151617")


class VoiceInputCryptoTests(unittest.TestCase):
    def test_all_domains_have_exact_nonce_layout_and_round_trip(self):
        domains = (
            READY_DOMAIN,
            UNAVAILABLE_DOMAIN,
            PTT_BEGIN_DOMAIN,
            AUDIO_DOMAIN,
            PTT_END_DOMAIN,
            VOICE_ERROR_DOMAIN,
        )
        for domain in domains:
            with self.subTest(domain=domain):
                nonce = derive_nonce(domain, TEST_NONCE, 0x04030201)
                self.assertEqual(
                    nonce,
                    bytes((domain,)) + TEST_NONCE + bytes.fromhex("01020304"),
                )
                ciphertext = encrypt(
                    TEST_KEY, domain, TEST_NONCE, 0x04030201, b"body", b"aad"
                )
                self.assertEqual(
                    decrypt(
                        TEST_KEY,
                        domain,
                        TEST_NONCE,
                        0x04030201,
                        ciphertext,
                        b"aad",
                    ),
                    b"body",
                )

    def test_loads_production_key_from_exact_keychain_item_with_injected_runner(self):
        calls = []

        def runner(command, **kwargs):
            calls.append((command, kwargs))
            return subprocess.CompletedProcess(
                command, 0, stdout=TEST_KEY.hex() + "\n", stderr=""
            )

        self.assertEqual(load_key_from_keychain(runner=runner), TEST_KEY)
        self.assertEqual(
            calls,
            [
                (
                    [
                        "security",
                        "find-generic-password",
                        "-s",
                        "com.stopwatch.codex-agent.voice",
                        "-a",
                        "StopWatch-Codex",
                        "-w",
                    ],
                    {"capture_output": True, "text": True, "check": False},
                )
            ],
        )

    def test_rejects_key_domain_nonce_sequence_and_authenticated_data_tampering(self):
        ciphertext = encrypt(
            TEST_KEY, AUDIO_DOMAIN, TEST_NONCE, 1, b"audio", b"header"
        )

        invalid_inputs = (
            lambda: encrypt(TEST_KEY[:-1], AUDIO_DOMAIN, TEST_NONCE, 1, b"", b""),
            lambda: derive_nonce(0x10, TEST_NONCE, 1),
            lambda: derive_nonce(AUDIO_DOMAIN, bytes(8), 1),
            lambda: derive_nonce(AUDIO_DOMAIN, TEST_NONCE[:-1], 1),
            lambda: derive_nonce(AUDIO_DOMAIN, TEST_NONCE, -1),
            lambda: derive_nonce(AUDIO_DOMAIN, TEST_NONCE, 0x100000000),
        )
        for operation in invalid_inputs:
            with self.subTest(operation=operation), self.assertRaises(ValueError):
                operation()

        tampered_ciphertext = bytearray(ciphertext)
        tampered_ciphertext[0] ^= 1
        tampered_tag = bytearray(ciphertext)
        tampered_tag[-1] ^= 1
        tampered_nonce = bytearray(TEST_NONCE)
        tampered_nonce[0] ^= 1
        failures = (
            (TEST_KEY, AUDIO_DOMAIN, TEST_NONCE, 1, bytes(tampered_ciphertext), b"header"),
            (TEST_KEY, AUDIO_DOMAIN, TEST_NONCE, 1, bytes(tampered_tag), b"header"),
            (TEST_KEY, AUDIO_DOMAIN, bytes(tampered_nonce), 1, ciphertext, b"header"),
            (TEST_KEY, AUDIO_DOMAIN, TEST_NONCE, 1, ciphertext, b"changed"),
            (bytes(reversed(TEST_KEY)), AUDIO_DOMAIN, TEST_NONCE, 1, ciphertext, b"header"),
            (TEST_KEY, PTT_END_DOMAIN, TEST_NONCE, 1, ciphertext, b"header"),
        )
        for arguments in failures:
            with self.subTest(arguments=arguments[:4]), self.assertRaises(InvalidTag):
                decrypt(*arguments)

    def test_keychain_failures_are_fail_closed_and_do_not_expose_output(self):
        secret = TEST_KEY.hex()

        def failed_runner(command, **_kwargs):
            return subprocess.CompletedProcess(command, 44, stdout=secret, stderr=secret)

        with self.assertRaises(VoiceKeyError) as failed:
            load_key_from_keychain(runner=failed_runner)
        self.assertNotIn(secret, str(failed.exception))

        malformed_values = (secret.upper(), secret[:-1], " " + secret, secret + " ")
        for value in malformed_values:
            with self.subTest(value=value):
                def malformed_runner(command, **_kwargs):
                    return subprocess.CompletedProcess(
                        command, 0, stdout=value + "\n", stderr=""
                    )

                with self.assertRaises(VoiceKeyError) as malformed:
                    load_key_from_keychain(runner=malformed_runner)
                self.assertNotIn(value, str(malformed.exception))

        def unavailable_runner(_command, **_kwargs):
            raise OSError("security unavailable")

        with self.assertRaises(VoiceKeyError):
            load_key_from_keychain(runner=unavailable_runner)


if __name__ == "__main__":
    unittest.main()
