import dataclasses
import enum
import hashlib
import hmac
import os
import pathlib
import signal
import subprocess
import threading
import time


BLACKHOLE_VERSION = "0.7.1"
BLACKHOLE_PACKAGE_NAME = "BlackHole2ch-0.7.1.pkg"
BLACKHOLE_URL = (
    "https://existential.audio/downloads/BlackHole2ch-0.7.1.pkg"
)
BLACKHOLE_SHA256 = (
    "57b540f27a3e29c37e310e01bee0fdfab76733087e47f997ef9dccf851400dcf"
)
BLACKHOLE_RECEIPT = "audio.existential.BlackHole2ch"
BLACKHOLE_DEVICE_NAME = "BlackHole 2ch"
INSTALL_SCRIPT = """on run argv
    set packagePath to item 1 of argv
    do shell script "/usr/sbin/installer -pkg " & quoted form of packagePath & " -target /" with administrator privileges
end run"""


class DriverState(enum.Enum):
    DRIVER_REQUIRED = "driver_required"
    RESTART_REQUIRED = "restart_required"
    READY = "ready"
    INSTALLING = "installing"
    ERROR = "error"


def verify_package(
    path: pathlib.Path, expected_sha256: str = BLACKHOLE_SHA256
) -> bool:
    digest = hashlib.sha256()
    with pathlib.Path(path).open("rb") as package:
        for chunk in iter(lambda: package.read(1024 * 1024), b""):
            digest.update(chunk)
    return hmac.compare_digest(digest.hexdigest(), expected_sha256)


def detect_driver_state(receipt_present: bool, device_visible: bool) -> DriverState:
    if device_visible:
        return DriverState.READY
    if receipt_present:
        return DriverState.RESTART_REQUIRED
    return DriverState.DRIVER_REQUIRED


def blackhole_receipt_present(run=None) -> bool:
    run = run or subprocess.run
    try:
        result = run(
            ["/usr/sbin/pkgutil", "--pkg-info", BLACKHOLE_RECEIPT],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=2.0,
        )
    except (OSError, subprocess.TimeoutExpired):
        return False
    return result.returncode == 0


def blackhole_device_visible() -> bool:
    from voice_audio import VoiceAudioBridge, VoiceAudioDeviceNotFound

    try:
        VoiceAudioBridge().find_device_uid(BLACKHOLE_DEVICE_NAME)
    except VoiceAudioDeviceNotFound:
        return False
    return True


class DriverInstallError(RuntimeError):
    pass


class DriverInstallCancelled(DriverInstallError):
    pass


@dataclasses.dataclass
class _InstallOperation:
    cancel_requested: threading.Event = dataclasses.field(
        default_factory=threading.Event
    )
    completed: threading.Event = dataclasses.field(default_factory=threading.Event)
    process: object = None


class BlackHoleInstaller:
    def __init__(
        self,
        *,
        expected_sha256=BLACKHOLE_SHA256,
        process_factory=None,
        signal_process_group=None,
        receipt_checker=None,
        device_checker=None,
        sleep=time.sleep,
        monotonic=time.monotonic,
        poll_interval=0.25,
        poll_timeout=5.0,
    ):
        self._expected_sha256 = expected_sha256
        self._process_factory = process_factory or subprocess.Popen
        self._signal_process_group = signal_process_group or os.killpg
        self._receipt_checker = receipt_checker
        self._device_checker = device_checker or blackhole_device_visible
        self._sleep = sleep
        self._monotonic = monotonic
        self._poll_interval = poll_interval
        self._poll_timeout = poll_timeout
        self._lock = threading.Lock()
        self._operation = None
        self._closed = False

    def install(self, package: pathlib.Path) -> DriverState:
        package = pathlib.Path(package)
        operation = self._reserve_operation()
        try:
            return self._install(operation, package)
        finally:
            self._complete_operation(operation)

    def _install(
        self, operation: _InstallOperation, package: pathlib.Path
    ) -> DriverState:
        self._raise_if_cancelled(operation)
        try:
            package_matches = verify_package(package, self._expected_sha256)
        except OSError as error:
            raise DriverInstallError("Driver package is unavailable") from error
        self._raise_if_cancelled(operation)
        if not package_matches:
            raise DriverInstallError("Driver package verification failed")

        command = ["/usr/bin/osascript", "-e", INSTALL_SCRIPT, str(package)]
        process = self._start_owned_process(
            operation,
            command,
            "Driver installer could not start",
        )
        returncode = self._wait_owned_process(operation, process)
        if returncode != 0:
            raise DriverInstallError("Driver installation was cancelled or failed")

        try:
            self._raise_if_cancelled(operation)
            if self._receipt_checker is not None:
                receipt_present = self._receipt_checker()
                self._raise_if_cancelled(operation)
            else:
                receipt_process = self._start_owned_process(
                    operation,
                    ["/usr/sbin/pkgutil", "--pkg-info", BLACKHOLE_RECEIPT],
                    "Driver receipt verification failed",
                )
                receipt_present = self._wait_owned_process(
                    operation,
                    receipt_process,
                    timeout=2.0,
                ) == 0
        except DriverInstallCancelled:
            raise
        except Exception as error:
            raise DriverInstallError("Driver receipt verification failed") from error
        if not receipt_present:
            raise DriverInstallError("Driver receipt verification failed")

        deadline = self._monotonic() + self._poll_timeout
        while True:
            self._raise_if_cancelled(operation)
            try:
                device_visible = self._device_checker()
            except Exception as error:
                raise DriverInstallError("Core Audio verification failed") from error
            self._raise_if_cancelled(operation)
            if device_visible:
                return DriverState.READY
            remaining = deadline - self._monotonic()
            if remaining <= 0:
                return DriverState.RESTART_REQUIRED
            self._sleep(min(self._poll_interval, remaining))

    def _reserve_operation(self) -> _InstallOperation:
        with self._lock:
            if self._closed:
                raise DriverInstallCancelled("Driver setup was cancelled")
            if self._operation is not None:
                raise DriverInstallError("Driver setup is already running")
            operation = _InstallOperation()
            self._operation = operation
            return operation

    def _complete_operation(self, operation: _InstallOperation) -> None:
        with self._lock:
            if self._operation is operation:
                operation.process = None
                operation.completed.set()
                self._operation = None
                return
        operation.completed.set()

    def cancel(self, timeout: float = 1.0) -> bool:
        with self._lock:
            operation = self._operation
            if operation is not None:
                operation.cancel_requested.set()
        if operation is None:
            return True
        return operation.completed.wait(max(0.0, timeout))

    def close(self, timeout: float = 1.0) -> bool:
        with self._lock:
            self._closed = True
            operation = self._operation
            if operation is not None:
                operation.cancel_requested.set()
        if operation is None:
            return True
        return operation.completed.wait(max(0.0, timeout))

    def _start_owned_process(
        self,
        operation: _InstallOperation,
        command,
        start_error: str,
    ):
        self._raise_if_cancelled(operation)
        try:
            process = self._process_factory(
                command,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
        except OSError as error:
            self._raise_if_cancelled(operation)
            raise DriverInstallError(start_error) from error

        with self._lock:
            current = (
                self._operation is operation
                and not self._closed
                and not operation.cancel_requested.is_set()
            )
            if current:
                operation.process = process
        if not current:
            self._terminate_and_reap(process, 0.5)
            raise DriverInstallCancelled("Driver setup was cancelled")
        return process

    def _wait_owned_process(
        self,
        operation: _InstallOperation,
        process,
        timeout=None,
    ) -> int:
        deadline = None if timeout is None else self._monotonic() + timeout
        while True:
            if operation.cancel_requested.is_set():
                if self._wait_until(process, self._monotonic()):
                    self._clear_process(operation, process)
                    raise DriverInstallCancelled("Driver setup was cancelled")
                reaped = self._terminate_and_reap(process, 0.5)
                if reaped:
                    self._clear_process(operation, process)
                raise DriverInstallCancelled("Driver setup was cancelled")

            wait_deadline = self._monotonic() + 0.02
            if deadline is not None:
                wait_deadline = min(wait_deadline, deadline)
            try:
                returncode = process.wait(
                    timeout=max(0.0, wait_deadline - self._monotonic())
                )
            except subprocess.TimeoutExpired:
                if deadline is not None and self._monotonic() >= deadline:
                    reaped = self._terminate_and_reap(process, 0.5)
                    if reaped:
                        self._clear_process(operation, process)
                    raise
                continue
            except Exception:
                reaped = self._terminate_and_reap(process, 0.5)
                if reaped:
                    self._clear_process(operation, process)
                raise
            self._clear_process(operation, process)
            self._raise_if_cancelled(operation)
            return returncode

    def _clear_process(self, operation: _InstallOperation, process) -> None:
        with self._lock:
            if self._operation is operation and operation.process is process:
                operation.process = None

    @staticmethod
    def _raise_if_cancelled(operation: _InstallOperation) -> None:
        if operation.cancel_requested.is_set():
            raise DriverInstallCancelled("Driver setup was cancelled")

    def _send_group_signal(self, process, signum: int) -> None:
        try:
            self._signal_process_group(process.pid, signum)
        except ProcessLookupError:
            pass
        except OSError:
            operation = process.terminate if signum == signal.SIGTERM else process.kill
            try:
                operation()
            except ProcessLookupError:
                pass

    def _terminate_and_reap(self, process, timeout: float) -> bool:
        deadline = self._monotonic() + max(0.0, timeout)
        if self._wait_until(process, self._monotonic()):
            return True
        self._send_group_signal(process, signal.SIGTERM)
        if self._wait_until(process, min(deadline, self._monotonic() + 0.25)):
            return True
        self._send_group_signal(process, signal.SIGKILL)
        return self._wait_until(process, deadline)

    def _wait_until(self, process, deadline: float) -> bool:
        timeout = max(0.0, deadline - self._monotonic())
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            return False
        return True
