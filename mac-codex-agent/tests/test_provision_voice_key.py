import importlib.util
import os
import pathlib
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock


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
        self.assertIn("stopwatch_configure_voice_key", cmake)
        key_module = (ROOT / "main/cmake/voice_key_build.cmake").read_text()
        self.assertIn("provision_voice_key.py", key_module)
        self.assertIn("add_custom_target", key_module)
        self.assertIn("add_dependencies", key_module)
        self.assertIn("firmware-header", key_module)
        self.assertIn("STOPWATCH_VOICE_KEY_FILE", key_module)
        self.assertIn("voice_input_device_key.h", ignore)

    @unittest.skipUnless(shutil.which("cmake"), "cmake is required")
    def test_incremental_build_revalidates_key_and_never_uses_stale_header(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source = root / "source"
            build = root / "build"
            source.mkdir()
            module = ROOT / "main/cmake/voice_key_build.cmake"
            source.joinpath("CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.16)\n"
                "project(voice_key_incremental LANGUAGES CXX)\n"
                "add_executable(firmware main.cpp)\n"
                f'include("{module}")\n'
                "stopwatch_configure_voice_key(firmware)\n"
            )
            source.joinpath("main.cpp").write_text(
                '#include "voice_input_device_key.h"\n'
                "int main() { return voice_input::generated::kDeviceKey[0]; }\n"
            )
            key_path = root / "device-key.hex"
            key_path.write_text("000102030405060708090a0b0c0d0e0f\n")
            key_path.chmod(0o600)
            environment = os.environ.copy()
            environment["STOPWATCH_VOICE_KEY_FILE"] = str(key_path)

            configured = subprocess.run(
                ["cmake", "-S", str(source), "-B", str(build)],
                env=environment,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(configured.returncode, 0, configured.stderr)

            def build_result():
                return subprocess.run(
                    ["cmake", "--build", str(build)],
                    env=environment,
                    capture_output=True,
                    text=True,
                    check=False,
                )

            first = build_result()
            self.assertEqual(first.returncode, 0, first.stderr)
            header = build / "generated/stopwatch_voice/voice_input_device_key.h"
            original_header = header.read_text()

            key_path.unlink()
            self.assertNotEqual(build_result().returncode, 0)

            key_path.write_text("0" * 32 + "\n")
            key_path.chmod(0o644)
            self.assertNotEqual(build_result().returncode, 0)

            key_path.chmod(0o600)
            key_path.write_text("malformed\n")
            self.assertNotEqual(build_result().returncode, 0)

            replacement = root / "replacement.hex"
            replacement.write_text("f0e0d0c0b0a090807060504030201000\n")
            replacement.chmod(0o600)
            os.replace(replacement, key_path)
            replaced = build_result()
            self.assertEqual(replaced.returncode, 0, replaced.stderr)
            self.assertNotEqual(header.read_text(), original_header)
            self.assertIn("0xf0, 0xe0, 0xd0, 0xc0", header.read_text())

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

    def test_concurrent_creators_publish_one_complete_key_without_temp_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            key_path = root / "device-key.hex"
            release = root / "release"
            workers = []
            worker_source = """
import importlib.util
import os
import pathlib
import sys
import time

script, key_path, ready_path, release_path, result_path = map(pathlib.Path, sys.argv[1:])
spec = importlib.util.spec_from_file_location("provision_voice_key", script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
original_link = os.link

def synchronized_link(*args, **kwargs):
    ready_path.write_text("ready")
    while not release_path.exists():
        time.sleep(0.005)
    return original_link(*args, **kwargs)

module.os.link = synchronized_link
result_path.write_text(module.ensure_key_file(key_path))
"""
            try:
                for index in range(6):
                    ready = root / f"ready-{index}"
                    result = root / f"result-{index}"
                    workers.append(
                        subprocess.Popen(
                            [
                                sys.executable,
                                "-c",
                                worker_source,
                                str(SCRIPT),
                                str(key_path),
                                str(ready),
                                str(release),
                                str(result),
                            ],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            text=True,
                        )
                    )
                deadline = time.monotonic() + 5.0
                ready_paths = [root / f"ready-{index}" for index in range(6)]
                while not all(path.exists() for path in ready_paths):
                    if time.monotonic() >= deadline:
                        self.fail("concurrent creators did not all reach atomic publish")
                    time.sleep(0.01)
                release.write_text("go")
                for worker in workers:
                    stdout, stderr = worker.communicate(timeout=10)
                    self.assertEqual(worker.returncode, 0, stdout + stderr)
            finally:
                release.touch(exist_ok=True)
                for worker in workers:
                    if worker.poll() is None:
                        worker.kill()
                    worker.communicate()

            encoded = key_path.read_text().strip()
            self.assertRegex(encoded, r"^[0-9a-f]{32}$")
            self.assertEqual(key_path.read_bytes(), (encoded + "\n").encode())
            self.assertEqual(stat.S_IMODE(key_path.stat().st_mode), 0o600)
            self.assertEqual(
                {root.joinpath(f"result-{index}").read_text() for index in range(6)},
                {encoded},
            )
            self.assertEqual(list(root.glob(".device-key.hex.*")), [])

    def test_key_creation_completes_bounded_short_writes(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temporary:
            key_path = pathlib.Path(temporary) / "device-key.hex"
            original_write = module.os.write

            def short_write(descriptor, contents):
                return original_write(descriptor, contents[:3])

            with mock.patch.object(module.os, "write", side_effect=short_write):
                encoded = module.ensure_key_file(key_path)

            self.assertEqual(key_path.read_bytes(), (encoded + "\n").encode())
            self.assertEqual(len(key_path.read_bytes()), module.MAX_KEY_FILE_BYTES)

    def test_publish_failure_after_fsync_leaves_no_final_or_temp_file(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            key_path = root / "device-key.hex"
            with mock.patch.object(
                module.os, "link", side_effect=OSError("injected publish failure")
            ):
                with self.assertRaisesRegex(
                    module.ProvisioningError, "cannot be published"
                ):
                    module.ensure_key_file(key_path)

            self.assertFalse(key_path.exists())
            self.assertEqual(list(root.glob(".device-key.hex.*")), [])

    def test_hard_exit_after_temp_fsync_never_exposes_partial_final_key(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            key_path = root / "device-key.hex"
            worker_source = """
import importlib.util
import os
import pathlib
import sys

script, key_path = map(pathlib.Path, sys.argv[1:])
spec = importlib.util.spec_from_file_location("provision_voice_key", script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def hard_exit_before_publish(*args, **kwargs):
    os._exit(73)

module.os.link = hard_exit_before_publish
module.ensure_key_file(key_path)
"""
            interrupted = subprocess.run(
                [
                    sys.executable,
                    "-c",
                    worker_source,
                    str(SCRIPT),
                    str(key_path),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(interrupted.returncode, 73)
            self.assertEqual(interrupted.stdout + interrupted.stderr, "")
            if key_path.exists():
                encoded = key_path.read_text().strip()
                self.assertRegex(encoded, r"^[0-9a-f]{32}$")
                self.assertEqual(key_path.read_bytes(), (encoded + "\n").encode())
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

    def test_rejects_symlink_paths_and_oversized_key_files(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            key_path = root / "device-key.hex"
            key_path.write_text("0" * 32 + "\n")
            key_path.chmod(0o600)

            key_link = root / "key-link.hex"
            key_link.symlink_to(key_path)
            with self.assertRaisesRegex(module.ProvisioningError, "regular file"):
                module.read_key_file(key_link)

            key_path.write_bytes(b"0" * 4096)
            key_path.chmod(0o600)
            with self.assertRaisesRegex(module.ProvisioningError, "malformed"):
                module.read_key_file(key_path)

            real_parent = root / "real-output"
            real_parent.mkdir()
            linked_parent = root / "linked-output"
            linked_parent.symlink_to(real_parent, target_is_directory=True)
            key_path.write_text("0" * 32 + "\n")
            key_path.chmod(0o600)
            with self.assertRaisesRegex(module.ProvisioningError, "parent"):
                module.generate_firmware_header(
                    key_path, linked_parent / "voice_input_device_key.h"
                )

            writable_parent = root / "writable-output"
            writable_parent.mkdir(mode=0o777)
            writable_parent.chmod(0o777)
            with self.assertRaisesRegex(module.ProvisioningError, "parent"):
                module.generate_firmware_header(
                    key_path, writable_parent / "voice_input_device_key.h"
                )

            output_parent = root / "safe-output"
            output_parent.mkdir()
            output = output_parent / "voice_input_device_key.h"
            output.symlink_to(root / "outside-header")
            with self.assertRaisesRegex(module.ProvisioningError, "output"):
                module.generate_firmware_header(key_path, output)

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
