"""Core Audio bridge and recoverable default-input routing for watch voice."""

import ctypes
from pathlib import Path
from typing import List, Optional, Sequence, Tuple


BLACKHOLE_DEVICE_NAME = "BlackHole 2ch"
LIBRARY_NAME = "libstopwatch_voice_audio.dylib"
UID_BUFFER_SIZE = 1024
INT16_MIN = -32768
INT16_MAX = 32767
NATIVE_DEVICE_NOT_FOUND = -2

VoiceAudioFailure = Tuple[str, Exception]


class VoiceAudioError(RuntimeError):
    """The native Core Audio bridge could not complete an operation."""

    def __init__(
        self,
        message: str,
        failures: Sequence[VoiceAudioFailure] = (),
    ):
        self.failures = tuple(failures)
        if self.failures:
            details = "; ".join(
                "{}: {}".format(operation, error)
                for operation, error in self.failures
            )
            message = "{}: {}".format(message, details)
        super().__init__(message)


class VoiceAudioDeviceNotFound(VoiceAudioError):
    """The requested Core Audio device is not currently visible."""


def _error_failures(
    operation: str, error: Exception
) -> List[VoiceAudioFailure]:
    if isinstance(error, VoiceAudioError) and error.failures:
        return list(error.failures)
    return [(operation, error)]


def _prioritize_cleanup_failures(
    failures: Sequence[VoiceAudioFailure],
) -> List[VoiceAudioFailure]:
    priority = {
        "restore default input": 0,
        "stop audio sink": 1,
        "close audio sink": 2,
    }
    return sorted(failures, key=lambda item: priority[item[0]])


def native_library_path() -> Path:
    """Return the dylib path for an installed app or a development checkout."""
    module_directory = Path(__file__).resolve().parent
    resources_directory = module_directory.parent
    if (
        module_directory.name == "app"
        and resources_directory.name == "Resources"
        and resources_directory.parent.name == "Contents"
    ):
        return resources_directory / "native" / LIBRARY_NAME
    return module_directory / ".native" / LIBRARY_NAME


class VoiceAudioBridge:
    def __init__(self, library=None):
        if library is None:
            library_path = native_library_path()
            try:
                library = ctypes.CDLL(str(library_path))
            except OSError as error:
                raise VoiceAudioError(
                    "could not load Core Audio bridge at {}".format(library_path)
                ) from error
        self._library = library
        self._configure_signatures()

    def _configure_signatures(self) -> None:
        self._library.swva_find_device_uid.argtypes = [
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char),
            ctypes.c_size_t,
        ]
        self._library.swva_find_device_uid.restype = ctypes.c_int
        self._library.swva_get_default_input_uid.argtypes = [
            ctypes.POINTER(ctypes.c_char),
            ctypes.c_size_t,
        ]
        self._library.swva_get_default_input_uid.restype = ctypes.c_int
        self._library.swva_set_default_input_uid.argtypes = [ctypes.c_char_p]
        self._library.swva_set_default_input_uid.restype = ctypes.c_int
        self._library.swva_open.argtypes = [ctypes.c_char_p]
        self._library.swva_open.restype = ctypes.c_int
        self._library.swva_start.argtypes = []
        self._library.swva_start.restype = ctypes.c_int
        self._library.swva_write_mono_16k.argtypes = [
            ctypes.POINTER(ctypes.c_int16),
            ctypes.c_size_t,
        ]
        self._library.swva_write_mono_16k.restype = ctypes.c_size_t
        self._library.swva_dropped_frames.argtypes = []
        self._library.swva_dropped_frames.restype = ctypes.c_uint64
        self._library.swva_stop.argtypes = []
        self._library.swva_stop.restype = None
        self._library.swva_close.argtypes = []
        self._library.swva_close.restype = None

    @staticmethod
    def _check_status(operation: str, status: int) -> None:
        if status != 0:
            raise VoiceAudioError("{} failed with status {}".format(operation, status))

    def _read_uid(self, operation: str, function, argument: Optional[str] = None) -> str:
        output = ctypes.create_string_buffer(UID_BUFFER_SIZE)
        if argument is None:
            status = function(output, len(output))
        else:
            status = function(argument.encode("utf-8"), output, len(output))
        if operation == "find device" and status == NATIVE_DEVICE_NOT_FOUND:
            raise VoiceAudioDeviceNotFound(
                "{} failed with status {}".format(operation, status)
            )
        self._check_status(operation, status)
        try:
            uid = output.value.decode("utf-8")
        except UnicodeDecodeError as error:
            raise VoiceAudioError("{} returned a non-UTF-8 device UID".format(operation)) from error
        if not uid:
            raise VoiceAudioError("{} returned an empty device UID".format(operation))
        return uid

    def find_device_uid(self, name: str) -> str:
        return self._read_uid("find device", self._library.swva_find_device_uid, name)

    def default_input_uid(self) -> str:
        return self._read_uid(
            "get default input", self._library.swva_get_default_input_uid
        )

    def set_default_input_uid(self, uid: str) -> None:
        self._check_status(
            "set default input",
            self._library.swva_set_default_input_uid(uid.encode("utf-8")),
        )

    def open(self, output_uid: str) -> None:
        self._check_status("open audio sink", self._library.swva_open(output_uid.encode("utf-8")))

    def start(self) -> None:
        self._check_status("start audio sink", self._library.swva_start())

    def write(self, samples: Sequence[int]) -> int:
        validated_samples = tuple(samples)
        for index, sample in enumerate(validated_samples):
            if sample < INT16_MIN or sample > INT16_MAX:
                raise ValueError(
                    "sample {} at index {} is outside int16 range".format(
                        sample, index
                    )
                )
        pcm = (ctypes.c_int16 * len(validated_samples))(*validated_samples)
        return int(
            self._library.swva_write_mono_16k(pcm, len(validated_samples))
        )

    def dropped_frames(self) -> int:
        return int(self._library.swva_dropped_frames())

    def stop(self) -> None:
        self._library.swva_stop()

    def close(self) -> None:
        self._library.swva_close()


class DefaultInputLease:
    def __init__(self, bridge: VoiceAudioBridge):
        self._bridge = bridge
        self._previous_uid: Optional[str] = None

    @property
    def acquired(self) -> bool:
        return self._previous_uid is not None

    def acquire(self, device_name: str) -> None:
        if self.acquired:
            return

        previous_uid = self._bridge.default_input_uid()
        target_uid = self._bridge.find_device_uid(device_name)
        self._previous_uid = previous_uid
        try:
            self._bridge.set_default_input_uid(target_uid)
        except Exception as switch_error:
            try:
                self.release()
            except Exception as restore_error:
                failures = [
                    ("switch default input", switch_error),
                    ("restore default input", restore_error),
                ]
                raise VoiceAudioError(
                    "could not acquire default input lease", failures
                ) from switch_error
            raise

    def release(self) -> None:
        if self._previous_uid is None:
            return

        previous_uid = self._previous_uid
        self._previous_uid = None
        self._bridge.set_default_input_uid(previous_uid)


class VoiceAudioRoute:
    def __init__(
        self,
        bridge: VoiceAudioBridge,
        device_name: str = BLACKHOLE_DEVICE_NAME,
    ):
        self._bridge = bridge
        self._device_name = device_name
        self._lease = DefaultInputLease(bridge)
        self._opened = False
        self._started = False

    def start(self) -> None:
        if self._started:
            return

        try:
            output_uid = self._bridge.find_device_uid(self._device_name)
            self._opened = True
            self._bridge.open(output_uid)
            self._bridge.start()
            self._lease.acquire(self._device_name)
            self._started = True
        except Exception as error:
            failures = _error_failures("start audio route", error)
            failures.extend(_prioritize_cleanup_failures(self._cleanup()))
            raise VoiceAudioError(
                "could not start audio route", failures
            ) from failures[0][1]

    def write(self, samples: Sequence[int]) -> int:
        if not self._started:
            raise VoiceAudioError("audio route is not started")
        return self._bridge.write(samples)

    def stop(self) -> None:
        failures = _prioritize_cleanup_failures(self._cleanup())
        if failures:
            raise VoiceAudioError(
                "could not clean up audio route", failures
            ) from failures[0][1]

    def _cleanup(self) -> List[VoiceAudioFailure]:
        failures = []
        if self._opened:
            try:
                self._bridge.stop()
            except Exception as error:
                failures.append(("stop audio sink", error))
            try:
                self._bridge.close()
            except Exception as error:
                failures.append(("close audio sink", error))
            self._opened = False

        if self._lease.acquired:
            try:
                self._lease.release()
            except Exception as error:
                failures.append(("restore default input", error))
        self._started = False
        return failures
