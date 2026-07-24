import enum
import hashlib
import hmac
import pathlib


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
