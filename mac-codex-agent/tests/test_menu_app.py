import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from AppKit import NSApplication, NSColor, NSControlStateValueOn

from app_state import AgentSnapshot, CodexSnapshot, ConnectionState
from menu_app import MenuBarApplication
from voice_driver import DriverState
from voice_session import VoiceState


class FakeController:
    def __init__(self, voice_state=VoiceState.READY):
        self.started = False
        self.stopped = False
        self.connection_values = []
        self.reconnect_count = 0
        self.voice_refresh_count = 0
        self.voice_state = voice_state

    def start(self):
        self.started = True

    def snapshot(self):
        return AgentSnapshot(
            ConnectionState.CONNECTED,
            True,
            voice_state=self.voice_state,
            codex=CodexSnapshot.from_status(
                {
                    "codex_model": "gpt-5.3",
                    "codex_effort": "high",
                    "codex_5h_left_pct": 72,
                    "codex_week_left_pct": 64,
                    "codex_alert_pending": 1,
                    "codex_alert_running": 1,
                    "codex_alert_completed": 1,
                    "codex_alert_revision": 4,
                    "codex_alerts": [
                        {
                            "project": "StopWatch",
                            "title": "Task alert support",
                            "time": "09:41",
                            "state": "pending",
                            "outcome": "waiting",
                        },
                        {
                            "project": "Docs",
                            "title": "Update guide",
                            "time": "09:02",
                            "state": "running",
                            "outcome": "running",
                        },
                        {
                            "project": "Website",
                            "title": "Header fix",
                            "time": "08:12",
                            "state": "completed",
                            "outcome": "completed",
                        },
                    ],
                }
            ),
        )

    def set_connection_enabled(self, enabled):
        self.connection_values.append(enabled)

    def reconnect(self):
        self.reconnect_count += 1

    def refresh_voice_readiness(self):
        self.voice_refresh_count += 1

    def stop(self, timeout=5.0):
        self.stopped = True
        return True


class MenuBarApplicationTests(unittest.TestCase):
    def test_install_completion_after_shutdown_does_not_refresh_runtime_or_menu(self):
        NSApplication.sharedApplication()
        controller = FakeController(VoiceState.DRIVER_REQUIRED)
        delegate = MenuBarApplication.alloc().initWithController_(controller)
        delegate.applicationDidFinishLaunching_(None)

        delegate._shutdown()
        delegate._finish_voice_setup(DriverState.READY)

        self.assertEqual(controller.voice_refresh_count, 0)
        self.assertFalse(delegate.setup_voice_item.isEnabled())
        self.assertFalse(delegate.accessibility_item.isEnabled())

    def test_setup_runs_off_appkit_and_disables_action_until_completion(self):
        class Installer:
            def __init__(self):
                self.packages = []

            def install(self, package):
                self.packages.append(package)
                return DriverState.READY

        class Worker:
            def __init__(self, target):
                self.target = target
                self.started = False

            def start(self):
                self.started = True

        NSApplication.sharedApplication()
        controller = FakeController(VoiceState.DRIVER_REQUIRED)
        delegate = MenuBarApplication.alloc().initWithController_(controller)
        delegate.applicationDidFinishLaunching_(None)
        self.addCleanup(delegate._shutdown)
        installer = Installer()
        workers = []
        package = pathlib.Path("/private/test/BlackHole package.pkg")
        delegate.voice_installer = installer
        delegate.voice_package_path = package
        delegate.voice_worker_factory = lambda target: workers.append(
            Worker(target)
        ) or workers[-1]
        delegate.call_after = lambda function, *args: function(*args)

        delegate.setupVoice_(None)

        self.assertEqual(installer.packages, [])
        self.assertEqual(len(workers), 1)
        self.assertTrue(workers[0].started)
        self.assertFalse(delegate.setup_voice_item.isEnabled())
        workers[0].target()
        self.assertEqual(installer.packages, [package])
        self.assertEqual(controller.voice_refresh_count, 1)
        self.assertTrue(delegate.setup_voice_item.isEnabled())

    def test_menu_shows_setup_only_when_driver_is_missing(self):
        NSApplication.sharedApplication()
        controller = FakeController(VoiceState.DRIVER_REQUIRED)
        delegate = MenuBarApplication.alloc().initWithController_(controller)
        delegate.applicationDidFinishLaunching_(None)
        self.addCleanup(delegate._shutdown)

        self.assertEqual(
            delegate.voice_label.stringValue(), "Voice Input: Driver Required"
        )
        self.assertFalse(delegate.voice_item.isEnabled())
        self.assertIsNone(delegate.voice_item.action())
        self.assertFalse(delegate.setup_voice_item.isHidden())
        self.assertTrue(delegate.accessibility_item.isHidden())
        self.assertEqual(
            delegate.menu.indexOfItem_(delegate.voice_item),
            delegate.menu.indexOfItem_(delegate.device_item) + 1,
        )

    def test_menu_uses_exact_snapshot_voice_state_for_row_and_actions(self):
        NSApplication.sharedApplication()
        controller = FakeController()
        delegate = MenuBarApplication.alloc().initWithController_(controller)
        delegate.applicationDidFinishLaunching_(None)
        self.addCleanup(delegate._shutdown)
        cases = (
            (VoiceState.DRIVER_REQUIRED, "Driver Required", False, True),
            (VoiceState.RESTART_REQUIRED, "Restart Required", True, True),
            (
                VoiceState.ACCESSIBILITY_REQUIRED,
                "Accessibility Required",
                True,
                False,
            ),
            (VoiceState.TYPELESS_NOT_RUNNING, "Typeless Not Running", True, True),
            (VoiceState.READY, "Ready", True, True),
            (VoiceState.RECORDING, "Recording", True, True),
            (VoiceState.ERROR, "Error", True, True),
        )

        for state, label, setup_hidden, accessibility_hidden in cases:
            with self.subTest(state=state):
                controller.voice_state = state
                delegate.refresh_(None)
                self.assertEqual(
                    delegate.voice_label.stringValue(),
                    "Voice Input: {}".format(label),
                )
                self.assertEqual(delegate.setup_voice_item.isHidden(), setup_hidden)
                self.assertEqual(
                    delegate.accessibility_item.isHidden(), accessibility_hidden
                )

    def test_accessibility_action_opens_the_system_settings_url(self):
        class Workspace:
            def __init__(self):
                self.urls = []

            def openURL_(self, url):
                self.urls.append(url.absoluteString())
                return True

        NSApplication.sharedApplication()
        delegate = MenuBarApplication.alloc().initWithController_(
            FakeController(VoiceState.ACCESSIBILITY_REQUIRED)
        )
        delegate.applicationDidFinishLaunching_(None)
        self.addCleanup(delegate._shutdown)
        workspace = Workspace()
        delegate.workspace = workspace

        delegate.openAccessibilitySettings_(None)

        self.assertEqual(
            workspace.urls,
            [
                "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"
            ],
        )

    def test_menu_lifecycle_and_actions(self):
        NSApplication.sharedApplication()
        controller = FakeController()
        delegate = MenuBarApplication.alloc().initWithController_(controller)

        delegate.applicationDidFinishLaunching_(None)

        self.assertTrue(controller.started)
        self.assertFalse(delegate.menu.autoenablesItems())
        self.assertEqual(delegate.codex_header_label.stringValue(), "Codex Status")
        self.assertEqual(delegate.model_label.stringValue(), "Model: gpt-5.3")
        self.assertEqual(delegate.effort_label.stringValue(), "Effort: High")
        self.assertEqual(delegate.five_hour_label.stringValue(), "5H Remaining: 72%")
        self.assertEqual(delegate.seven_day_label.stringValue(), "7D Remaining: 64%")
        self.assertFalse(hasattr(delegate, "today_label"))
        self.assertFalse(hasattr(delegate, "this_week_label"))
        self.assertEqual(
            delegate.alert_item.title(),
            "Task Alerts · 1 Pending / 1 Running / 1 Completed",
        )
        self.assertTrue(delegate.alert_item.isEnabled())
        self.assertEqual(delegate.alert_submenu.numberOfItems(), 6)
        self.assertEqual(
            [label.stringValue() for label in delegate._alert_labels],
            [
                "Pending",
                "StopWatch · Waiting for Instruction · Task alert support · 09:41",
                "Running",
                "Docs · Running · Update guide · 09:02",
                "Completed",
                "Website · Completed · Header fix · 08:12",
            ],
        )
        self.assertTrue(
            delegate._alert_labels[1].textColor().isEqual_(NSColor.systemOrangeColor())
        )
        self.assertTrue(
            delegate._alert_labels[3].textColor().isEqual_(NSColor.systemBlueColor())
        )
        self.assertTrue(
            delegate._alert_labels[5].textColor().isEqual_(NSColor.secondaryLabelColor())
        )
        self.assertEqual(
            delegate.connection_header_label.stringValue(), "Connection Status"
        )
        for item in (
            delegate.codex_header_item,
            delegate.model_item,
            delegate.effort_item,
            delegate.five_hour_item,
            delegate.seven_day_item,
            delegate.connection_header_item,
            delegate.agent_item,
            delegate.bluetooth_item,
            delegate.device_item,
        ):
            self.assertFalse(item.isEnabled())
            self.assertIsNone(item.action())
            self.assertIsNotNone(item.view())
        for label in (
            delegate.codex_header_label,
            delegate.model_label,
            delegate.effort_label,
            delegate.five_hour_label,
            delegate.seven_day_label,
            delegate.connection_header_label,
            delegate.agent_label,
            delegate.bluetooth_label,
            delegate.device_label,
        ):
            self.assertTrue(label.textColor().isEqual_(NSColor.labelColor()))
        self.assertEqual(
            delegate.model_label.frame().origin.x
            - delegate.codex_header_label.frame().origin.x,
            16,
        )
        self.assertEqual(
            delegate.agent_label.frame().origin.x
            - delegate.connection_header_label.frame().origin.x,
            16,
        )
        self.assertNotEqual(
            delegate.codex_header_label.font().fontName(),
            delegate.model_label.font().fontName(),
        )
        self.assertEqual(delegate.bluetooth_label.stringValue(), "Bluetooth: Connected")
        self.assertEqual(delegate.connection_item.title(), "Enable Watch Connection")
        self.assertEqual(delegate.connection_item.state(), NSControlStateValueOn)
        self.assertEqual(delegate.status_item.button().title(), " Codex")
        self.assertIsNotNone(delegate.status_item.button().image())

        delegate.toggleConnection_(delegate.connection_item)
        delegate.reconnect_(None)

        self.assertEqual(controller.connection_values, [False])
        self.assertEqual(controller.reconnect_count, 1)

        delegate._shutdown()
        self.assertTrue(controller.stopped)
        self.assertIsNone(delegate.status_item)


if __name__ == "__main__":
    unittest.main()
