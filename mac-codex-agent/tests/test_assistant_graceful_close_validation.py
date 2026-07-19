import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT_PATH = ROOT / "scripts" / "validate_assistant_graceful_close.py"


def load_module():
    spec = importlib.util.spec_from_file_location(
        "validate_assistant_graceful_close", SCRIPT_PATH
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GOOD_TRANSCRIPT = """
I AssistantTrace: ASTRACE sid=4 seq=1 t=100 comp=app event=open state=stopped detail=none result=accepted v0=0 v1=0 v2=0 v3=0
I AssistantTrace: ASTRACE sid=4 seq=8 t=200 comp=runtime event=snapshot state=running detail=first-running result=ok v0=100 v1=200 v2=18 v3=1
I AssistantTrace: ASTRACE sid=4 seq=9 t=300 comp=app event=close-request state=stopping detail=debug-home result=accepted v0=0 v1=0 v2=0 v3=0
I AssistantTrace: ASTRACE sid=4 seq=10 t=301 comp=runtime event=state state=stopping detail=stop result=accepted v0=0 v1=0 v2=0 v3=0
I AssistantTrace: ASTRACE sid=4 seq=11 t=350 comp=callback event=callback state=stopping detail=stopped result=accepted v0=0 v1=0 v2=0 v3=0
I AssistantTrace: ASTRACE sid=4 seq=12 t=360 comp=runtime event=release state=stopped detail=all-resources result=ok v0=0 v1=0 v2=0 v3=0
I AssistantTrace: ASTRACE sid=4 seq=13 t=370 comp=app event=closed state=stopped detail=debug-home result=ok v0=0 v1=0 v2=0 v3=0
I Launcher: on open
I DebugCommand: ASTATUS sid=4 state=stopped bindings=0 subscriptions=0 wifi=0 capture=0 playback=0 ap=0 blocked=0 internal=100 psram=200 tasks=18
"""


class AssistantGracefulCloseValidationTests(unittest.TestCase):
    def test_accepts_complete_ordered_session(self):
        module = load_module()
        validator = module.AssistantSessionValidator()
        for line in GOOD_TRANSCRIPT.splitlines():
            validator.feed(line)
        self.assertEqual(validator.running_session_id, 4)
        validator.require_complete(4)

    def test_rejects_missing_stopped_callback(self):
        module = load_module()
        validator = module.AssistantSessionValidator()
        for line in GOOD_TRANSCRIPT.replace(
            "detail=stopped result=accepted", "detail=unexpected result=accepted"
        ).splitlines():
            validator.feed(line)
        with self.assertRaisesRegex(module.ValidationError, "stopped callback"):
            validator.require_complete(4)

    def test_rejects_reset_and_dirty_resources(self):
        module = load_module()
        validator = module.AssistantSessionValidator()
        with self.assertRaisesRegex(module.ValidationError, "reset marker"):
            validator.feed("rst:0xc (SW_CPU_RESET),boot:0x8")

        validator = module.AssistantSessionValidator()
        for line in GOOD_TRANSCRIPT.replace("playback=0", "playback=1").splitlines():
            validator.feed(line)
        with self.assertRaisesRegex(module.ValidationError, "resource status"):
            validator.require_complete(4)


if __name__ == "__main__":
    unittest.main()
