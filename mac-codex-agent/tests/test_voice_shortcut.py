import pathlib
import sys
import unittest
from unittest import mock


AGENT_ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO_ROOT = AGENT_ROOT.parent
sys.path.insert(0, str(AGENT_ROOT))

from voice_shortcut import (  # noqa: E402
    AccessibilityRequired,
    FnShortcutController,
    LEFT_OPTION_KEY_CODE,
    TypelessNotRunning,
    _post_left_option,
)


class FnShortcutControllerTests(unittest.TestCase):
    def test_default_shortcut_posts_left_option(self):
        with mock.patch(
            "voice_shortcut.Quartz.CGEventCreateKeyboardEvent"
        ) as create:
            with mock.patch("voice_shortcut.Quartz.CGEventPost") as post:
                event = object()
                create.return_value = event

                _post_left_option(True)

        self.assertEqual(LEFT_OPTION_KEY_CODE, 0x3A)
        create.assert_called_once_with(None, LEFT_OPTION_KEY_CODE, True)
        post.assert_called_once_with(mock.ANY, event)

    def test_start_and_stop_emit_complete_shortcut_taps(self):
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
