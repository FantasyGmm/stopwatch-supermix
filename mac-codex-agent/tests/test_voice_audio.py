import ctypes
import pathlib
import sys
import unittest
from unittest import mock


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import voice_audio
from voice_audio import (
    DefaultInputLease,
    VoiceAudioBridge,
    VoiceAudioDeviceNotFound,
    VoiceAudioError,
    VoiceAudioRoute,
)


class FakeBridge:
    def __init__(
        self,
        default_uid="BuiltInMic",
        devices=None,
        fail_open=False,
        fail_start=False,
        fail_stop=False,
        fail_close=False,
        set_failures=None,
    ):
        self.default_uid = default_uid
        self.devices = devices or {"BlackHole 2ch": "BH2"}
        self.fail_open = fail_open
        self.fail_start = fail_start
        self.fail_stop = fail_stop
        self.fail_close = fail_close
        self.set_failures = {
            uid: list(failures) for uid, failures in (set_failures or {}).items()
        }
        self.events = []
        self.release_sets = 0

    def find_device_uid(self, name):
        self.events.append(("find", name))
        return self.devices[name]

    def default_input_uid(self):
        self.events.append(("default",))
        return self.default_uid

    def set_default_input_uid(self, uid):
        self.events.append(("set", uid))
        if uid == "BuiltInMic":
            self.release_sets += 1
        self.default_uid = uid
        failures = self.set_failures.get(uid)
        if failures:
            raise failures.pop(0)

    def open(self, output_uid):
        self.events.append(("open", output_uid))
        if self.fail_open:
            raise VoiceAudioError("open failed")

    def start(self):
        self.events.append(("start",))
        if self.fail_start:
            raise VoiceAudioError("start failed")

    def write(self, samples):
        self.events.append(("write", tuple(samples)))
        return len(samples)

    def dropped_frames(self):
        return 0

    def stop(self):
        self.events.append(("stop",))
        if self.fail_stop:
            raise VoiceAudioError("stop failed")

    def close(self):
        self.events.append(("close",))
        if self.fail_close:
            raise VoiceAudioError("close failed")


class FakeFunction:
    def __init__(self, result=0):
        self.result = result
        self.argtypes = None
        self.restype = None

    def __call__(self, *_args):
        return self.result


class FakeLibrary:
    def __init__(self):
        self.swva_find_device_uid = FakeFunction()
        self.swva_get_default_input_uid = FakeFunction()
        self.swva_set_default_input_uid = FakeFunction()
        self.swva_open = FakeFunction()
        self.swva_start = FakeFunction()
        self.swva_write_mono_16k = FakeFunction()
        self.swva_dropped_frames = FakeFunction()
        self.swva_stop = FakeFunction()
        self.swva_close = FakeFunction()


class DefaultInputLeaseTests(unittest.TestCase):
    def test_route_restores_the_exact_previous_uid(self):
        bridge = FakeBridge(
            default_uid="BuiltInMic", devices={"BlackHole 2ch": "BH2"}
        )
        lease = DefaultInputLease(bridge)

        lease.acquire("BlackHole 2ch")

        self.assertEqual(bridge.default_uid, "BH2")
        lease.release()
        self.assertEqual(bridge.default_uid, "BuiltInMic")

    def test_duplicate_release_does_not_overwrite_restored_input(self):
        bridge = FakeBridge(default_uid="USB-Microphone")
        lease = DefaultInputLease(bridge)
        lease.acquire("BlackHole 2ch")

        lease.release()
        lease.release()

        self.assertEqual(bridge.default_uid, "USB-Microphone")
        self.assertEqual(bridge.events.count(("set", "USB-Microphone")), 1)

    def test_failed_acquire_attempts_restore_only_once(self):
        bridge = FakeBridge(
            set_failures={
                "BH2": [VoiceAudioError("switch failed")],
                "BuiltInMic": [VoiceAudioError("restore failed")],
            }
        )
        route = VoiceAudioRoute(bridge)

        with self.assertRaises(VoiceAudioError):
            route.start()
        route.stop()

        set_sequence = [
            event[1] for event in bridge.events if event[0] == "set"
        ]
        self.assertEqual(set_sequence, ["BH2", "BuiltInMic"])


class VoiceAudioRouteTests(unittest.TestCase):
    def test_route_primes_sink_before_switching_default_input(self):
        bridge = FakeBridge(default_uid="BuiltInMic")
        route = VoiceAudioRoute(bridge)

        route.start()

        self.assertLess(
            bridge.events.index(("start",)), bridge.events.index(("set", "BH2"))
        )
        route.stop()
        self.assertEqual(bridge.default_uid, "BuiltInMic")

    def test_route_restores_after_sink_open_failure(self):
        bridge = FakeBridge(default_uid="BuiltInMic", fail_open=True)
        route = VoiceAudioRoute(bridge)

        with self.assertRaises(VoiceAudioError):
            route.start()

        self.assertEqual(bridge.default_uid, "BuiltInMic")
        self.assertIn(("close",), bridge.events)

    def test_open_error_remains_primary_when_close_also_fails(self):
        bridge = FakeBridge(fail_open=True, fail_close=True)
        route = VoiceAudioRoute(bridge)

        with self.assertRaises(VoiceAudioError) as caught:
            route.start()

        self.assertEqual(
            [name for name, _error in caught.exception.failures],
            ["start audio route", "close audio sink"],
        )
        self.assertEqual(str(caught.exception.__cause__), "open failed")
        self.assertIn("open failed", str(caught.exception))
        self.assertIn("close failed", str(caught.exception))

    def test_route_restores_after_sink_start_failure(self):
        bridge = FakeBridge(default_uid="BuiltInMic", fail_start=True)
        route = VoiceAudioRoute(bridge)

        with self.assertRaises(VoiceAudioError):
            route.start()

        self.assertEqual(bridge.default_uid, "BuiltInMic")
        self.assertIn(("close",), bridge.events)

    def test_stop_restores_input_when_sink_stop_and_close_fail(self):
        bridge = FakeBridge(
            default_uid="External-Mic", fail_stop=True, fail_close=True
        )
        route = VoiceAudioRoute(bridge)
        route.start()

        with self.assertRaises(VoiceAudioError):
            route.stop()

        self.assertEqual(bridge.default_uid, "External-Mic")
        self.assertEqual(bridge.events.count(("set", "External-Mic")), 1)

    def test_restore_error_has_priority_and_cleanup_aggregates_all_errors(self):
        bridge = FakeBridge(
            default_uid="External-Mic",
            fail_stop=True,
            fail_close=True,
            set_failures={
                "External-Mic": [VoiceAudioError("restore failed")],
            },
        )
        route = VoiceAudioRoute(bridge)
        route.start()

        with self.assertRaises(VoiceAudioError) as caught:
            route.stop()

        self.assertEqual(
            [name for name, _error in caught.exception.failures],
            ["restore default input", "stop audio sink", "close audio sink"],
        )
        self.assertEqual(str(caught.exception.__cause__), "restore failed")
        self.assertIn("restore failed", str(caught.exception))
        self.assertIn("stop failed", str(caught.exception))
        self.assertIn("close failed", str(caught.exception))


class VoiceAudioBridgeTests(unittest.TestCase):
    def test_write_reports_accepted_16khz_input_frames(self):
        library = FakeLibrary()
        library.swva_write_mono_16k.result = 960
        bridge = VoiceAudioBridge(library=library)

        self.assertEqual(bridge.write([0] * 320), 320)

    def test_find_device_exposes_native_not_found_as_typed_result(self):
        library = FakeLibrary()
        library.swva_find_device_uid.result = -2
        bridge = VoiceAudioBridge(library=library)

        with self.assertRaises(VoiceAudioDeviceNotFound):
            bridge.find_device_uid("BlackHole 2ch")

    def test_find_device_preserves_unexpected_core_audio_error(self):
        library = FakeLibrary()
        library.swva_find_device_uid.result = 1234
        bridge = VoiceAudioBridge(library=library)

        with self.assertRaisesRegex(VoiceAudioError, "status 1234") as caught:
            bridge.find_device_uid("BlackHole 2ch")

        self.assertNotIsInstance(caught.exception, VoiceAudioDeviceNotFound)

    def test_configures_all_ctypes_function_signatures(self):
        library = FakeLibrary()

        VoiceAudioBridge(library=library)

        self.assertEqual(
            library.swva_find_device_uid.argtypes,
            [ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t],
        )
        self.assertIs(library.swva_find_device_uid.restype, ctypes.c_int)
        self.assertEqual(
            library.swva_get_default_input_uid.argtypes,
            [ctypes.POINTER(ctypes.c_char), ctypes.c_size_t],
        )
        self.assertIs(library.swva_get_default_input_uid.restype, ctypes.c_int)
        self.assertEqual(
            library.swva_set_default_input_uid.argtypes, [ctypes.c_char_p]
        )
        self.assertIs(library.swva_set_default_input_uid.restype, ctypes.c_int)
        self.assertEqual(library.swva_open.argtypes, [ctypes.c_char_p])
        self.assertIs(library.swva_open.restype, ctypes.c_int)
        self.assertEqual(library.swva_start.argtypes, [])
        self.assertIs(library.swva_start.restype, ctypes.c_int)
        self.assertEqual(
            library.swva_write_mono_16k.argtypes,
            [ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t],
        )
        self.assertIs(library.swva_write_mono_16k.restype, ctypes.c_size_t)
        self.assertEqual(library.swva_dropped_frames.argtypes, [])
        self.assertIs(library.swva_dropped_frames.restype, ctypes.c_uint64)
        self.assertEqual(library.swva_stop.argtypes, [])
        self.assertIsNone(library.swva_stop.restype)
        self.assertEqual(library.swva_close.argtypes, [])
        self.assertIsNone(library.swva_close.restype)

    def test_uses_app_resource_library_when_running_bundled(self):
        bundled_module = (
            "/Applications/StopWatch Codex Agent.app/Contents/Resources/app/"
            "voice_audio.py"
        )
        with mock.patch.object(voice_audio, "__file__", bundled_module):
            library_path = voice_audio.native_library_path()

        self.assertEqual(
            library_path,
            pathlib.Path(
                "/Applications/StopWatch Codex Agent.app/Contents/Resources/native/"
                "libstopwatch_voice_audio.dylib"
            ),
        )

    def test_write_accepts_int16_boundaries_and_rejects_values_outside_them(self):
        bridge = VoiceAudioBridge(library=FakeLibrary())

        for sample in (-32768, 32767):
            with self.subTest(sample=sample):
                bridge.write([sample])

        for sample in (-32769, 32768):
            with self.subTest(sample=sample):
                with self.assertRaises(ValueError):
                    bridge.write([sample])


if __name__ == "__main__":
    unittest.main()
