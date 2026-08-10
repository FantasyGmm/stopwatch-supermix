import importlib.util
import pathlib
import subprocess
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/provision_voice_key.py"


def load_module():
    spec = importlib.util.spec_from_file_location("provision_voice_key", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class LegacyVoiceKeyMigrationTests(unittest.TestCase):
    def test_production_build_has_no_fixed_key_input_or_generated_header(self):
        cmake = (ROOT / "main/CMakeLists.txt").read_text(encoding="utf-8")
        ignore = (ROOT / ".gitignore").read_text(encoding="utf-8")
        runner = (ROOT / "scripts/run_unified_offline_validation.sh").read_text(
            encoding="utf-8"
        )

        self.assertFalse((ROOT / "main/cmake/voice_key_build.cmake").exists())
        for text in ("STOPWATCH_VOICE_KEY_FILE", "voice_input_device_key.h"):
            self.assertNotIn(text, cmake)
            self.assertNotIn(text, ignore)
            self.assertNotIn(text, runner)

    def test_cleanup_targets_only_the_exact_legacy_keychain_item(self):
        module = load_module()
        calls = []

        def runner(command, **kwargs):
            calls.append((command, kwargs))
            return subprocess.CompletedProcess(command, 0, "", "")

        self.assertTrue(module.remove_legacy_voice_key(runner=runner))
        self.assertEqual(
            calls[0][0],
            [
                "/usr/bin/security",
                "delete-generic-password",
                "-s",
                "com.stopwatch.codex-agent.voice",
                "-a",
                "StopWatch-Codex",
            ],
        )
        self.assertEqual(calls[0][1]["timeout"], 10.0)

    def test_cleanup_is_idempotent_when_item_is_absent(self):
        module = load_module()

        def runner(command, **_kwargs):
            return subprocess.CompletedProcess(
                command, module.KEYCHAIN_ITEM_NOT_FOUND_EXIT, "", ""
            )

        self.assertFalse(module.remove_legacy_voice_key(runner=runner))

    def test_cleanup_failure_is_redacted(self):
        module = load_module()
        secret = "00112233445566778899aabbccddeeff"

        def runner(command, **_kwargs):
            return subprocess.CompletedProcess(command, 1, "", secret)

        with self.assertRaises(module.MigrationError) as error:
            module.remove_legacy_voice_key(runner=runner)
        self.assertNotIn(secret, str(error.exception))

    def test_cli_exposes_cleanup_only_and_never_generates_or_imports_keys(self):
        module = load_module()
        source = SCRIPT.read_text(encoding="utf-8")
        for forbidden in (
            "token_hex",
            "add-generic-password",
            "firmware-header",
            "ensure_key_file",
            "import_keychain",
        ):
            self.assertNotIn(forbidden, source)

        with mock.patch.object(
            module, "remove_legacy_voice_key", return_value=True
        ) as cleanup:
            self.assertEqual(module.main(["remove-legacy-voice-key"]), 0)
        cleanup.assert_called_once_with()


if __name__ == "__main__":
    unittest.main()
