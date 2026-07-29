import pathlib
import sys
import unittest


AGENT_ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO_ROOT = AGENT_ROOT.parent
sys.path.insert(0, str(AGENT_ROOT))

from voice_shortcut import (  # noqa: E402
    AccessibilityRequired,
    FnShortcutController,
    TypelessNotRunning,
)


class FnShortcutControllerTests(unittest.TestCase):
    def test_start_and_stop_emit_complete_fn_taps(self):
        events = []
        controller = FnShortcutController(
            trusted=lambda: True,
            typeless_running=lambda: True,
            post_key=lambda down: events.append(down),
            sleep=lambda _seconds: None,
        )

        controller.start()
        controller.stop()

        self.assertEqual(events, [True, False, True, False])
        self.assertFalse(controller.active)

    def test_start_requires_accessibility_before_typeless_check(self):
        typeless_checks = []
        controller = FnShortcutController(
            trusted=lambda: False,
            typeless_running=lambda: typeless_checks.append(True),
        )

        with self.assertRaises(AccessibilityRequired):
            controller.start()

        self.assertEqual(typeless_checks, [])
        self.assertFalse(controller.active)

    def test_start_requires_typeless_and_never_arms(self):
        events = []
        controller = FnShortcutController(
            trusted=lambda: True,
            typeless_running=lambda: False,
            post_key=lambda down: events.append(down),
        )

        with self.assertRaises(TypelessNotRunning):
            controller.start()

        controller.stop()
        self.assertEqual(events, [])
        self.assertFalse(controller.active)

    def test_repeated_stop_is_idempotent(self):
        events = []
        controller = FnShortcutController(
            trusted=lambda: True,
            typeless_running=lambda: True,
            post_key=lambda down: events.append(down),
            sleep=lambda _seconds: None,
        )

        controller.start()
        controller.stop()
        controller.stop()

        self.assertEqual(events, [True, False, True, False])

    def test_requirements_pin_importable_quartz(self):
        requirements = (AGENT_ROOT / "requirements.txt").read_text(encoding="utf-8")
        self.assertIn("pyobjc-framework-Quartz>=11.1,<12", requirements.splitlines())
        self.assertIn(
            "pyobjc-framework-ApplicationServices>=11.1,<12",
            requirements.splitlines(),
        )

        import ApplicationServices
        import Quartz

        self.assertIsNotNone(ApplicationServices.AXIsProcessTrustedWithOptions)
        self.assertIsNotNone(Quartz.CGEventCreateKeyboardEvent)


if __name__ == "__main__":
    unittest.main()
