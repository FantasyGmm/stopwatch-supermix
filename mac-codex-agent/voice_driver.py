import enum
import hashlib
import hmac
import pathlib
import subprocess
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
        )
    except OSError:
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


class BlackHoleInstaller:
    def __init__(
        self,
        *,
        expected_sha256=BLACKHOLE_SHA256,
        run=None,
        receipt_checker=None,
        device_checker=None,
        sleep=time.sleep,
        monotonic=time.monotonic,
        poll_interval=0.25,
        poll_timeout=5.0,
    ):
        self._expected_sha256 = expected_sha256
        self._run = run or subprocess.run
        self._receipt_checker = receipt_checker or (
            lambda: blackhole_receipt_present(self._run)
        )
        self._device_checker = device_checker or blackhole_device_visible
        self._sleep = sleep
        self._monotonic = monotonic
        self._poll_interval = poll_interval
        self._poll_timeout = poll_timeout

    def install(self, package: pathlib.Path) -> DriverState:
        package = pathlib.Path(package)
        if not verify_package(package, self._expected_sha256):
            raise DriverInstallError("BlackHole package hash does not match the pin")
        self._run(
            ["/usr/bin/osascript", "-e", INSTALL_SCRIPT, str(package)],
            check=True,
        )
        if not self._receipt_checker():
            raise DriverInstallError("BlackHole package receipt was not installed")
        deadline = self._monotonic() + self._poll_timeout
        while True:
            if self._device_checker():
                return DriverState.READY
            remaining = deadline - self._monotonic()
            if remaining <= 0:
                return DriverState.RESTART_REQUIRED
            self._sleep(min(self._poll_interval, remaining))
