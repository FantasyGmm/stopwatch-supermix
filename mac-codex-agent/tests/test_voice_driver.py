import hashlib
import pathlib
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from voice_driver import (
    INSTALL_SCRIPT,
    BlackHoleInstaller,
    DriverInstallCancelled,
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

    def test_unreaped_cancel_retains_operation_until_worker_wait_succeeds(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        process_started = threading.Event()
        kill_sent = threading.Event()
        process_released = threading.Event()
        owner_threads = []
        group_signals = []
        install_errors = []
        self.addCleanup(process_released.set)

        class Process:
            pid = 2468

            def wait(self, timeout=None):
                owner_threads.append(("wait", threading.get_ident()))
                if not process_released.is_set():
                    time.sleep(min(timeout or 0.0, 0.02))
                    raise subprocess.TimeoutExpired("osascript", timeout)
                return -signal.SIGKILL

        def process_factory(_command, **_kwargs):
            process_started.set()
            return Process()

        def signal_process_group(_pid, signum):
            owner_threads.append(("signal", threading.get_ident()))
            group_signals.append(signum)
            if signum == signal.SIGKILL:
                kill_sent.set()

        installer = BlackHoleInstaller(
            expected_sha256=hashlib.sha256(b"known package").hexdigest(),
            process_factory=process_factory,
            signal_process_group=signal_process_group,
            receipt_checker=lambda: True,
            device_checker=lambda: True,
        )
        worker = threading.Thread(
            target=lambda: self._capture_error(
                install_errors, lambda: installer.install(package)
            ),
            daemon=True,
        )
        worker.start()
        self.assertTrue(process_started.wait(1))

        self.assertFalse(installer.cancel(timeout=0.05))
        self.assertTrue(kill_sent.wait(1))
        with self.assertRaisesRegex(
            DriverInstallError,
            "Driver setup is already running",
        ):
            installer.install(package)
        self.assertFalse(installer.close(timeout=0.35))
        self.assertTrue(worker.is_alive())
        self.assertEqual(group_signals, [signal.SIGTERM, signal.SIGKILL])
        self.assertTrue(
            all(thread_id == worker.ident for _, thread_id in owner_threads)
        )

        process_released.set()
        worker.join(1)

        self.assertFalse(worker.is_alive())
        self.assertTrue(installer.close(timeout=0.05))
        self.assertEqual(len(install_errors), 1)
        self.assertIsInstance(install_errors[0], DriverInstallCancelled)

    def test_close_stays_bounded_while_process_factory_is_blocked(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        factory_started = threading.Event()
        release_factory = threading.Event()
        process_released = threading.Event()
        group_signals = []
        install_errors = []

        class Process:
            pid = 7654

            def wait(self, timeout=None):
                if process_released.is_set():
                    return -signal.SIGTERM
                raise subprocess.TimeoutExpired("osascript", timeout)

        def process_factory(_command, **_kwargs):
            factory_started.set()
            release_factory.wait(1)
            return Process()

        def signal_process_group(_pid, signum):
            group_signals.append(signum)
            process_released.set()

        installer = BlackHoleInstaller(
            expected_sha256=hashlib.sha256(b"known package").hexdigest(),
            process_factory=process_factory,
            signal_process_group=signal_process_group,
            receipt_checker=lambda: True,
            device_checker=lambda: True,
        )
        worker = threading.Thread(
            target=lambda: self._capture_error(
                install_errors, lambda: installer.install(package)
            )
        )
        worker.start()
        self.assertTrue(factory_started.wait(1))
        delayed_release = threading.Timer(0.3, release_factory.set)
        delayed_release.start()

        started = time.monotonic()
        closed = installer.close(timeout=0.05)
        elapsed = time.monotonic() - started
        worker.join(1)
        delayed_release.cancel()

        self.assertFalse(closed)
        self.assertLess(elapsed, 0.2)
        self.assertFalse(worker.is_alive())
        self.assertEqual(group_signals, [signal.SIGTERM])
        self.assertEqual(len(install_errors), 1)

    def test_cancel_after_process_exit_never_signals_reaped_process_group(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        receipt_started = threading.Event()
        release_receipt = threading.Event()
        group_signals = []
        install_errors = []

        class Process:
            pid = 8765

            def wait(self, timeout=None):
                return 0

        def receipt_checker():
            receipt_started.set()
            release_receipt.wait(1)
            return True

        installer = BlackHoleInstaller(
            expected_sha256=hashlib.sha256(b"known package").hexdigest(),
            process_factory=lambda *_args, **_kwargs: Process(),
            signal_process_group=lambda pid, signum: group_signals.append(
                (pid, signum)
            ),
            receipt_checker=receipt_checker,
            device_checker=lambda: True,
        )
        worker = threading.Thread(
            target=lambda: self._capture_error(
                install_errors, lambda: installer.install(package)
            )
        )
        worker.start()
        self.assertTrue(receipt_started.wait(1))

        cancelled = installer.cancel(timeout=0.05)
        release_receipt.set()
        worker.join(1)

        self.assertFalse(cancelled)
        self.assertFalse(worker.is_alive())
        self.assertEqual(group_signals, [])
        self.assertEqual(len(install_errors), 1)
        self.assertIsInstance(install_errors[0], DriverInstallCancelled)

    def test_close_reports_incomplete_while_device_checker_is_blocked(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        device_check_started = threading.Event()
        release_device_check = threading.Event()
        group_signals = []
        install_errors = []

        class Process:
            pid = 9876

            def wait(self, timeout=None):
                return 0

        def device_checker():
            device_check_started.set()
            release_device_check.wait(1)
            return True

        installer = BlackHoleInstaller(
            expected_sha256=hashlib.sha256(b"known package").hexdigest(),
            process_factory=lambda *_args, **_kwargs: Process(),
            signal_process_group=lambda pid, signum: group_signals.append(
                (pid, signum)
            ),
            receipt_checker=lambda: True,
            device_checker=device_checker,
        )
        worker = threading.Thread(
            target=lambda: self._capture_error(
                install_errors, lambda: installer.install(package)
            )
        )
        worker.start()
        self.assertTrue(device_check_started.wait(1))

        started = time.monotonic()
        closed = installer.close(timeout=0.05)
        elapsed = time.monotonic() - started
        release_device_check.set()
        worker.join(1)

        self.assertFalse(closed)
        self.assertLess(elapsed, 0.2)
        self.assertFalse(worker.is_alive())
        self.assertEqual(group_signals, [])
        self.assertEqual(len(install_errors), 1)
        self.assertIsInstance(install_errors[0], DriverInstallCancelled)

    def test_install_can_retry_after_failure(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        returncodes = iter((1, 0))

        class Process:
            pid = 5432

            def __init__(self, returncode):
                self.returncode = returncode

            def wait(self, timeout=None):
                return self.returncode

        installer = BlackHoleInstaller(
            expected_sha256=hashlib.sha256(b"known package").hexdigest(),
            process_factory=lambda *_args, **_kwargs: Process(
                next(returncodes)
            ),
            receipt_checker=lambda: True,
            device_checker=lambda: True,
        )

        with self.assertRaisesRegex(
            DriverInstallError,
            "Driver installation was cancelled or failed",
        ):
            installer.install(package)

        self.assertIs(installer.install(package), DriverState.READY)

    def test_install_can_retry_after_cancelled_owned_operation(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        process_started = threading.Event()
        process_released = threading.Event()
        install_errors = []
        attempts = []

        class BlockedProcess:
            pid = 6543

            def wait(self, timeout=None):
                if process_released.is_set():
                    return -signal.SIGTERM
                raise subprocess.TimeoutExpired("osascript", timeout)

        def process_factory(*_args, **_kwargs):
            attempts.append(True)
            if len(attempts) == 1:
                process_started.set()
                return BlockedProcess()
            return CompletedProcess()

        installer = BlackHoleInstaller(
            expected_sha256=hashlib.sha256(b"known package").hexdigest(),
            process_factory=process_factory,
            signal_process_group=lambda _pid, _signum: process_released.set(),
            receipt_checker=lambda: True,
            device_checker=lambda: True,
        )
        worker = threading.Thread(
            target=lambda: self._capture_error(
                install_errors, lambda: installer.install(package)
            )
        )
        worker.start()
        self.assertTrue(process_started.wait(1))

        self.assertTrue(installer.cancel(timeout=1))
        worker.join(1)

        self.assertFalse(worker.is_alive())
        self.assertEqual(len(install_errors), 1)
        self.assertIsInstance(install_errors[0], DriverInstallCancelled)
        self.assertIs(installer.install(package), DriverState.READY)
        self.assertEqual(len(attempts), 2)

    @staticmethod
    def _capture_error(errors, operation):
        try:
            operation()
        except Exception as error:
            errors.append(error)


if __name__ == "__main__":
    unittest.main()
