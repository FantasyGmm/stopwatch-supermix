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
    ):
        self.default_uid = default_uid
        self.devices = devices or {"BlackHole 2ch": "BH2"}
        self.fail_open = fail_open
        self.fail_start = fail_start
        self.fail_stop = fail_stop
        self.fail_close = fail_close
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


class VoiceAudioBridgeTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
