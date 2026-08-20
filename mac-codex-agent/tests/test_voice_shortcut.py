import pathlib
import sys
import tempfile
import unittest
from unittest import mock


AGENT_ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO_ROOT = AGENT_ROOT.parent
sys.path.insert(0, str(AGENT_ROOT))

from voice_shortcut import (  # noqa: E402
    AccessibilityRequired,
    FnShortcutController,
    TypelessNotRunning,
    TypelessShortcutUnavailable,
    _post_key,
)


class FnShortcutControllerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.settings = pathlib.Path(self.directory.name) / "app-settings.json"
        self.settings.write_text(
            '{"featureShortcutBindings": '
            '{"dictationMode": ["LeftOption"]}}',
            encoding="utf-8",
        )

    def test_startup_loads_current_typeless_dictation_chord_once(self):
        with tempfile.TemporaryDirectory() as directory:
            settings = pathlib.Path(directory) / "app-settings.json"
            settings.write_text(
                '{"featureShortcutBindings": '
                '{"dictationMode": ["LeftOption+Space"]}}',
                encoding="utf-8",
            )
            events = []
            try:
                controller = FnShortcutController(
                    trusted=lambda: True,
                    typeless_running=lambda: True,
                    post_key=lambda key_code, down: events.append(
                        (key_code, down)
                    ),
                    sleep=lambda _seconds: None,
                    settings_path=settings,
                )
            except TypeError as error:
                self.fail(f"controller cannot load Typeless settings: {error}")

            settings.write_text(
                '{"featureShortcutBindings": '
                '{"dictationMode": ["LeftCtrl"]}}',
                encoding="utf-8",
            )
            controller.start()
            controller.stop()

        self.assertEqual(
            events,
            [
                (0x3A, True),
                (0x31, True),
                (0x31, False),
                (0x3A, False),
                (0x3A, True),
                (0x31, True),
                (0x31, False),
                (0x3A, False),
            ],
        )

    def test_post_key_uses_resolved_virtual_key_code(self):
        with mock.patch(
            "voice_shortcut.Quartz.CGEventCreateKeyboardEvent"
        ) as create:
            with mock.patch("voice_shortcut.Quartz.CGEventPost") as post:
                event = object()
                create.return_value = event

                _post_key(0x31, True)

        create.assert_called_once_with(None, 0x31, True)
        post.assert_called_once_with(mock.ANY, event)

    def test_start_and_stop_emit_complete_shortcut_taps(self):
        events = []
        controller = FnShortcutController(
            trusted=lambda: True,
            typeless_running=lambda: True,
            post_key=lambda key_code, down: events.append((key_code, down)),
            sleep=lambda _seconds: None,
            settings_path=self.settings,
        )

        controller.start()
        controller.stop()

        self.assertEqual(
            events,
            [(0x3A, True), (0x3A, False), (0x3A, True), (0x3A, False)],
        )
        self.assertFalse(controller.active)

    def test_start_requires_accessibility_before_typeless_check(self):
        typeless_checks = []
        controller = FnShortcutController(
            trusted=lambda: False,
            typeless_running=lambda: typeless_checks.append(True),
            settings_path=self.settings,
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
            post_key=lambda key_code, down: events.append((key_code, down)),
            settings_path=self.settings,
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
            post_key=lambda key_code, down: events.append((key_code, down)),
            sleep=lambda _seconds: None,
            settings_path=self.settings,
        )

        controller.start()
        controller.stop()
        controller.stop()

        self.assertEqual(
            events,
            [(0x3A, True), (0x3A, False), (0x3A, True), (0x3A, False)],
        )

    def test_unknown_startup_shortcut_fails_closed_without_fallback(self):
        self.settings.write_text(
            '{"featureShortcutBindings": '
            '{"dictationMode": ["HyperKey"]}}',
            encoding="utf-8",
        )
        events = []
        controller = FnShortcutController(
            trusted=lambda: True,
            typeless_running=lambda: True,
            post_key=lambda key_code, down: events.append((key_code, down)),
            settings_path=self.settings,
        )

        with self.assertRaisesRegex(
            TypelessShortcutUnavailable,
            "Unsupported Typeless shortcut key: HyperKey",
        ):
            controller.start()

        self.assertEqual(events, [])
        self.assertFalse(controller.active)

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
