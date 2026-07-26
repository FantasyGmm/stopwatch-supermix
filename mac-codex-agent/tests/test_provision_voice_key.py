import importlib.util
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/provision_voice_key.py"


def load_module():
    spec = importlib.util.spec_from_file_location("provision_voice_key", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ProvisionVoiceKeyTests(unittest.TestCase):
    def test_firmware_cmake_generates_ignored_header_and_fails_closed(self):
        cmake = (ROOT / "main/CMakeLists.txt").read_text()
        ignore = (ROOT / ".gitignore").read_text()
        self.assertIn("provision_voice_key.py", cmake)
        self.assertIn("firmware-header", cmake)
        self.assertIn("RESULT_VARIABLE", cmake)
        self.assertIn("message(FATAL_ERROR", cmake)
        self.assertIn("STOPWATCH_VOICE_GENERATED_INCLUDE", cmake)
        self.assertIn("voice_input_device_key.h", ignore)

    def test_creates_one_lowercase_key_with_newline_and_mode_0600(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temporary:
            home = pathlib.Path(temporary)
            key_path = module.default_key_path(home)
            first = module.ensure_key_file(key_path)
            second = module.ensure_key_file(key_path)

            self.assertEqual(first, second)
            self.assertRegex(first, r"^[0-9a-f]{32}$")
            self.assertEqual(key_path.read_bytes(), (first + "\n").encode())
            self.assertEqual(stat.S_IMODE(key_path.stat().st_mode), 0o600)

    def test_key_mode_is_0600_even_with_a_restrictive_process_umask(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temporary:
            key_path = pathlib.Path(temporary) / "device-key.hex"
            previous_umask = os.umask(0o777)
            try:
                module.ensure_key_file(key_path)
            finally:
                os.umask(previous_umask)
            self.assertEqual(stat.S_IMODE(key_path.stat().st_mode), 0o600)

    def test_rejects_malformed_or_insecure_existing_key(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temporary:
            key_path = pathlib.Path(temporary) / "device-key.hex"
            invalid_values = (
                b"0" * 31 + b"\n",
                b"A" * 32 + b"\n",
                b"0" * 32,
                b"0" * 32 + b"\n\n",
            )
            for value in invalid_values:
                with self.subTest(value=value):
                    key_path.write_bytes(value)
                    key_path.chmod(0o600)
                    with self.assertRaisesRegex(module.ProvisioningError, "malformed"):
                        module.read_key_file(key_path)

            key_path.write_bytes(b"0" * 32 + b"\n")
            key_path.chmod(0o644)
            with self.assertRaisesRegex(module.ProvisioningError, "permissions"):
                module.read_key_file(key_path)

    def test_keychain_import_uses_stdin_and_never_exposes_secret(self):
        module = load_module()
        secret = "00112233445566778899aabbccddeeff"
        calls = []

        def runner(command, **kwargs):
            calls.append((command, kwargs))
            return subprocess.CompletedProcess(command, 0, stdout="", stderr="")

        module.import_keychain(secret, runner=runner)
        command, kwargs = calls[0]
        self.assertNotIn(secret, command)
        self.assertEqual(command[-1], "-w")
        self.assertEqual(kwargs["input"], secret + "\n")
        self.assertFalse(kwargs["check"])
        self.assertNotIn(secret, repr(command))

        def failed_runner(command, **kwargs):
            return subprocess.CompletedProcess(
                command, 44, stdout=secret, stderr=secret
            )

        with self.assertRaises(module.ProvisioningError) as failed:
            module.import_keychain(secret, runner=failed_runner)
        self.assertNotIn(secret, str(failed.exception))

    def test_generates_restricted_header_and_fails_closed(self):
        module = load_module()
        secret = "000102030405060708090a0b0c0d0e0f"
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            key_path = root / "device-key.hex"
            output = root / "build/generated/voice_input_device_key.h"
            key_path.write_text(secret + "\n")
            key_path.chmod(0o600)

            module.generate_firmware_header(key_path, output)
            header = output.read_text()
            self.assertIn("kDeviceKey", header)
            self.assertIn("0x00, 0x01, 0x02, 0x03", header)
            self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o600)

            key_path.unlink()
            with self.assertRaisesRegex(module.ProvisioningError, "missing"):
                module.generate_firmware_header(key_path, output)

    def test_cli_honors_override_and_fake_security_without_secret_argv(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            key_path = root / "keys/device-key.hex"
            header = root / "build/generated/voice_input_device_key.h"
            argv_capture = root / "argv.txt"
            stdin_capture = root / "stdin.txt"
            fake_security = root / "security"
            fake_security.write_text(
                "#!/bin/sh\n"
                "printf '%s\\n' \"$@\" > \"$ARGV_CAPTURE\"\n"
                "cat > \"$STDIN_CAPTURE\"\n"
            )
            fake_security.chmod(0o755)
            environment = os.environ.copy()
            environment.update(
                {
                    "HOME": str(root),
                    "STOPWATCH_VOICE_KEY_FILE": str(key_path),
                    "STOPWATCH_SECURITY_COMMAND": str(fake_security),
                    "ARGV_CAPTURE": str(argv_capture),
                    "STDIN_CAPTURE": str(stdin_capture),
                }
            )
            result = subprocess.run(
                [sys.executable, str(SCRIPT), "provision", "--firmware-header", str(header)],
                env=environment,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            secret = key_path.read_text().strip()
            self.assertNotIn(secret, argv_capture.read_text())
            self.assertEqual(stdin_capture.read_text(), secret + "\n")
            self.assertNotIn(secret, result.stdout + result.stderr)
            self.assertTrue(header.is_file())


if __name__ == "__main__":
    unittest.main()
