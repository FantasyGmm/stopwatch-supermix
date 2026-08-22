import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT_PATH = ROOT / "scripts" / "watch_debug.py"


def load_module():
    spec = importlib.util.spec_from_file_location("watch_debug", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class WatchDebugTests(unittest.TestCase):
    def test_discovers_exactly_one_watch_port(self):
        module = load_module()
        self.assertEqual(
            module.discover_port(["/dev/cu.usbmodem1101"]),
            "/dev/cu.usbmodem1101",
        )
        with self.assertRaises(module.PortDiscoveryError):
            module.discover_port([])
        with self.assertRaises(module.PortDiscoveryError):
            module.discover_port(
                ["/dev/cu.usbmodem1101", "/dev/cu.usbmodem2101"]
            )

    def test_builds_normalized_protocol_lines(self):
        module = load_module()
        self.assertEqual(module.build_command("list"), b"debug app list\n")
        self.assertEqual(module.build_command("home"), b"debug app home\n")
        self.assertEqual(
            module.build_command("open", "Assistant"),
            b"debug app open assistant\n",
        )
        self.assertEqual(
            module.build_command("open", "Audio.FFT"),
            b"debug app open audio-fft\n",
        )
        self.assertEqual(
            module.build_command("assistant-home"), b"debug assistant home\n"
        )
        self.assertEqual(
            module.build_command("assistant-status"), b"debug assistant status\n"
        )
        self.assertEqual(
            module.build_command("assistant-trace"), b"debug assistant trace\n"
        )
        self.assertEqual(
            module.build_command("assistant-trace-clear"),
            b"debug assistant trace clear\n",
        )
        self.assertEqual(
            module.build_command("assistant-wifi-cycle"),
            b"debug assistant wifi-cycle\n",
        )
        self.assertEqual(module.build_command("volume", 0), b"debug volume 0\n")
        with self.assertRaises(module.DebugCommandError):
            module.build_command("volume", 101)

    def test_classifies_matching_responses_and_errors(self):
        module = load_module()
        self.assertEqual(
            module.classify_response(
                "open", "assistant", "I tag: DEBUG APP OPEN assistant OK"
            ),
            module.ResponseStatus.SUCCESS,
        )
        self.assertEqual(
            module.classify_response(
                "open", "assistant", "DEBUG APP OPEN assistant BUSY"
            ),
            module.ResponseStatus.REJECTED,
        )
        self.assertEqual(
            module.classify_response(
                "open", "assistant", "DEBUG APP OPEN assistant NOT_FOUND"
            ),
            module.ResponseStatus.REJECTED,
        )
        self.assertEqual(
            module.classify_response(
                "list", None, "DEBUG APP LIST codex,assistant,clock"
            ),
            module.ResponseStatus.SUCCESS,
        )
        self.assertEqual(
            module.classify_response("home", None, "DEBUG APP HOME OK"),
            module.ResponseStatus.SUCCESS,
        )
        self.assertEqual(
            module.classify_response("home", None, "DEBUG APP HOME BUSY"),
            module.ResponseStatus.REJECTED,
        )
        self.assertEqual(
            module.classify_response("open", "assistant", "unrelated log"),
            module.ResponseStatus.PENDING,
        )
        self.assertEqual(
            module.classify_response(
                "assistant-home", None, "DEBUG ASSISTANT HOME OK"
            ),
            module.ResponseStatus.SUCCESS,
        )
        self.assertEqual(
            module.classify_response(
                "assistant-status", None, "DEBUG ASSISTANT STATUS BUSY"
            ),
            module.ResponseStatus.REJECTED,
        )
        self.assertEqual(
            module.classify_response(
                "assistant-trace-clear", None, "DEBUG ASSISTANT TRACE CLEAR OK"
            ),
            module.ResponseStatus.SUCCESS,
        )
        self.assertEqual(
            module.classify_response(
                "assistant-wifi-cycle", None, "DEBUG ASSISTANT WIFI CYCLE OK"
            ),
            module.ResponseStatus.SUCCESS,
        )
        self.assertEqual(
            module.classify_response("volume", 0, "DEBUG VOLUME 0 OK"),
            module.ResponseStatus.SUCCESS,
        )
        with self.assertRaises(module.DebugCommandError):
            module.build_command("battery-start")


if __name__ == "__main__":
    unittest.main()
