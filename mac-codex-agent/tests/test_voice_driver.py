import hashlib
import pathlib
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from voice_driver import (
    INSTALL_SCRIPT,
    BlackHoleInstaller,
    DriverInstallError,
    DriverState,
    detect_driver_state,
    verify_package,
)


class VoiceDriverTests(unittest.TestCase):
    def setUp(self):
        self._temporary_directory = tempfile.TemporaryDirectory()
        self.directory = pathlib.Path(self._temporary_directory.name)

    def tearDown(self):
        self._temporary_directory.cleanup()

    def test_accepts_only_the_pinned_blackhole_package(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        expected = hashlib.sha256(b"known package").hexdigest()
        self.assertTrue(verify_package(package, expected))
        self.assertFalse(verify_package(package, "0" * 64))

    def test_classifies_missing_receipt_and_visible_device(self):
        self.assertEqual(
            detect_driver_state(False, False), DriverState.DRIVER_REQUIRED
        )
        self.assertEqual(
            detect_driver_state(True, False), DriverState.RESTART_REQUIRED
        )
        self.assertEqual(detect_driver_state(True, True), DriverState.READY)

    def test_installer_uses_osascript_argv_without_shell_interpolation(self):
        package = self.directory / "BlackHole package; touch should-not-run.pkg"
        package.write_bytes(b"known package")
        expected = hashlib.sha256(b"known package").hexdigest()
        calls = []

        def run(command, **kwargs):
            calls.append((command, kwargs))

        installer = BlackHoleInstaller(
            expected_sha256=expected,
            run=run,
            receipt_checker=lambda: True,
            device_checker=lambda: True,
        )

        state = installer.install(package)

        self.assertIs(state, DriverState.READY)
        self.assertEqual(
            calls,
            [
                (
                    [
                        "/usr/bin/osascript",
                        "-e",
                        INSTALL_SCRIPT,
                        str(package),
                    ],
                    {"check": True},
                )
            ],
        )

    def test_installer_polls_core_audio_after_receipt_verification(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        expected = hashlib.sha256(b"known package").hexdigest()
        visibility = iter((False, False, True))
        elapsed = [0.0]
        sleeps = []

        def sleep(seconds):
            sleeps.append(seconds)
            elapsed[0] += seconds

        installer = BlackHoleInstaller(
            expected_sha256=expected,
            run=lambda *_args, **_kwargs: None,
            receipt_checker=lambda: True,
            device_checker=lambda: next(visibility),
            sleep=sleep,
            monotonic=lambda: elapsed[0],
            poll_interval=0.25,
        )

        state = installer.install(package)

        self.assertIs(state, DriverState.READY)
        self.assertEqual(sleeps, [0.25, 0.25])

    def test_installer_verifies_the_pinned_receipt_after_authorization(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        expected = hashlib.sha256(b"known package").hexdigest()
        calls = []

        def run(command, **kwargs):
            calls.append((command, kwargs))
            return SimpleNamespace(returncode=0)

        installer = BlackHoleInstaller(
            expected_sha256=expected,
            run=run,
            device_checker=lambda: True,
        )

        state = installer.install(package)

        self.assertIs(state, DriverState.READY)
        self.assertEqual(calls[0][0][0], "/usr/bin/osascript")
        self.assertEqual(
            calls[1],
            (
                [
                    "/usr/sbin/pkgutil",
                    "--pkg-info",
                    "audio.existential.BlackHole2ch",
                ],
                {
                    "stdout": subprocess.DEVNULL,
                    "stderr": subprocess.DEVNULL,
                    "check": False,
                },
            ),
        )

    def test_hash_mismatch_fails_before_authorization_or_post_install_checks(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"tampered package")
        calls = []
        installer = BlackHoleInstaller(
            expected_sha256="0" * 64,
            run=lambda *_args, **_kwargs: calls.append("authorization"),
            receipt_checker=lambda: calls.append("receipt") or True,
            device_checker=lambda: calls.append("device") or True,
        )

        with self.assertRaisesRegex(DriverInstallError, "hash"):
            installer.install(package)

        self.assertEqual(calls, [])

    def test_installer_returns_restart_required_after_five_second_poll(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        expected = hashlib.sha256(b"known package").hexdigest()
        elapsed = [0.0]
        sleeps = []

        def sleep(seconds):
            sleeps.append(seconds)
            elapsed[0] += seconds

        installer = BlackHoleInstaller(
            expected_sha256=expected,
            run=lambda *_args, **_kwargs: None,
            receipt_checker=lambda: True,
            device_checker=lambda: False,
            sleep=sleep,
            monotonic=lambda: elapsed[0],
            poll_interval=2.0,
        )

        state = installer.install(package)

        self.assertIs(state, DriverState.RESTART_REQUIRED)
        self.assertEqual(len(sleeps), 3)
        self.assertAlmostEqual(sum(sleeps), 5.0)


if __name__ == "__main__":
    unittest.main()
