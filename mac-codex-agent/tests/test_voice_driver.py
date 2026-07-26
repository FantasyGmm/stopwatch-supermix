import hashlib
import pathlib
import signal
import subprocess
import sys
import tempfile
import threading
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from voice_driver import (
    INSTALL_SCRIPT,
    BlackHoleInstaller,
    DriverInstallError,
    DriverState,
    blackhole_receipt_present,
    detect_driver_state,
    verify_package,
)


class CompletedProcess:
    pid = 1234

    def __init__(self, returncode=0):
        self.returncode = returncode

    def wait(self, timeout=None):
        return self.returncode


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

        def process_factory(command, **kwargs):
            calls.append((command, kwargs))
            return CompletedProcess()

        installer = BlackHoleInstaller(
            expected_sha256=expected,
            process_factory=process_factory,
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
                    {
                        "stdout": subprocess.DEVNULL,
                        "stderr": subprocess.DEVNULL,
                        "start_new_session": True,
                    },
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
            process_factory=lambda *_args, **_kwargs: CompletedProcess(),
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

        def receipt_run(command, **kwargs):
            calls.append((command, kwargs))
            return CompletedProcess()

        def process_factory(command, **kwargs):
            calls.append((command, kwargs))
            return CompletedProcess()

        installer = BlackHoleInstaller(
            expected_sha256=expected,
            process_factory=process_factory,
            receipt_checker=lambda: blackhole_receipt_present(receipt_run),
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
                    "timeout": 2.0,
                },
            ),
        )

    def test_default_receipt_check_is_an_owned_process_group(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        expected = hashlib.sha256(b"known package").hexdigest()
        calls = []

        def process_factory(command, **kwargs):
            calls.append((command, kwargs))
            return CompletedProcess()

        installer = BlackHoleInstaller(
            expected_sha256=expected,
            process_factory=process_factory,
            device_checker=lambda: True,
        )

        state = installer.install(package)

        self.assertIs(state, DriverState.READY)
        self.assertEqual(calls[0][0][0], "/usr/bin/osascript")
        self.assertEqual(
            calls[1][0],
            [
                "/usr/sbin/pkgutil",
                "--pkg-info",
                "audio.existential.BlackHole2ch",
            ],
        )
        self.assertTrue(calls[1][1]["start_new_session"])

    def test_hash_mismatch_fails_before_authorization_or_post_install_checks(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"tampered package")
        calls = []
        installer = BlackHoleInstaller(
            expected_sha256="0" * 64,
            process_factory=lambda *_args, **_kwargs: calls.append("authorization"),
            receipt_checker=lambda: calls.append("receipt") or True,
            device_checker=lambda: calls.append("device") or True,
        )

        with self.assertRaisesRegex(DriverInstallError, "verification"):
            installer.install(package)

        self.assertEqual(calls, [])

    def test_missing_package_has_concise_visible_failure(self):
        installer = BlackHoleInstaller(
            process_factory=lambda *_args, **_kwargs: self.fail(
                "missing package reached authorization"
            )
        )

        with self.assertRaisesRegex(
            DriverInstallError, "^Driver package is unavailable$"
        ):
            installer.install(self.directory / "missing.pkg")

    def test_installer_launch_failure_has_concise_visible_failure(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")

        def fail_to_start(*_args, **_kwargs):
            raise OSError("host detail must not escape")

        installer = BlackHoleInstaller(
            expected_sha256=hashlib.sha256(b"known package").hexdigest(),
            process_factory=fail_to_start,
        )

        with self.assertRaisesRegex(
            DriverInstallError, "^Driver installer could not start$"
        ):
            installer.install(package)

    def test_authorization_cancellation_has_concise_visible_failure(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        installer = BlackHoleInstaller(
            expected_sha256=hashlib.sha256(b"known package").hexdigest(),
            process_factory=lambda *_args, **_kwargs: CompletedProcess(1),
        )

        with self.assertRaisesRegex(
            DriverInstallError,
            "^Driver installation was cancelled or failed$",
        ):
            installer.install(package)

    def test_receipt_failure_has_concise_visible_failure(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        installer = BlackHoleInstaller(
            expected_sha256=hashlib.sha256(b"known package").hexdigest(),
            process_factory=lambda *_args, **_kwargs: CompletedProcess(),
            receipt_checker=lambda: False,
        )

        with self.assertRaisesRegex(
            DriverInstallError, "^Driver receipt verification failed$"
        ):
            installer.install(package)

    def test_core_audio_failure_has_concise_visible_failure(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")

        def fail_device_check():
            raise RuntimeError("device secret 0123456789abcdef0123456789abcdef")

        installer = BlackHoleInstaller(
            expected_sha256=hashlib.sha256(b"known package").hexdigest(),
            process_factory=lambda *_args, **_kwargs: CompletedProcess(),
            receipt_checker=lambda: True,
            device_checker=fail_device_check,
        )

        with self.assertRaisesRegex(
            DriverInstallError, "^Core Audio verification failed$"
        ):
            installer.install(package)

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
            process_factory=lambda *_args, **_kwargs: CompletedProcess(),
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

    def test_cancel_terminates_kills_and_reaps_blocked_process_group(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        expected = hashlib.sha256(b"known package").hexdigest()
        process_started = threading.Event()
        process_released = threading.Event()
        process_calls = []
        group_signals = []
        install_errors = []

        class Process:
            pid = 4321

            def wait(self, timeout=None):
                process_calls.append(("wait", timeout))
                if timeout is None:
                    process_released.wait(1)
                elif not process_released.is_set():
                    raise subprocess.TimeoutExpired("osascript", timeout)
                return -signal.SIGKILL

        def process_factory(command, **kwargs):
            process_calls.append(("start", command, kwargs))
            process_started.set()
            return Process()

        def signal_process_group(pid, signum):
            group_signals.append((pid, signum))
            if signum == signal.SIGKILL:
                process_released.set()

        installer = BlackHoleInstaller(
            expected_sha256=expected,
            process_factory=process_factory,
            signal_process_group=signal_process_group,
            receipt_checker=lambda: self.fail("receipt checked after cancellation"),
            device_checker=lambda: self.fail("device checked after cancellation"),
        )
        worker = threading.Thread(
            target=lambda: self._capture_error(
                install_errors, lambda: installer.install(package)
            )
        )
        worker.start()
        self.assertTrue(process_started.wait(1))

        reaped = installer.cancel(timeout=0.05)
        worker.join(0.2)

        self.assertTrue(reaped)
        self.assertFalse(worker.is_alive())
        self.assertEqual(
            group_signals,
            [(4321, signal.SIGTERM), (4321, signal.SIGKILL)],
        )
        self.assertEqual(process_calls[0][0], "start")
        self.assertTrue(process_calls[0][2]["start_new_session"])
        self.assertGreaterEqual(
            len([call for call in process_calls if call[0] == "wait"]), 3
        )
        self.assertEqual(len(install_errors), 1)

    @staticmethod
    def _capture_error(errors, operation):
        try:
            operation()
        except Exception as error:
            errors.append(error)


if __name__ == "__main__":
    unittest.main()
