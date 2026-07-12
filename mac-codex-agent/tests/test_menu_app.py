import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from AppKit import NSApplication, NSColor, NSControlStateValueOn

from app_state import AgentSnapshot, CodexSnapshot, ConnectionState
from menu_app import MenuBarApplication


class FakeController:
    def __init__(self):
        self.started = False
        self.stopped = False
        self.connection_values = []
        self.reconnect_count = 0

    def start(self):
        self.started = True

    def snapshot(self):
        return AgentSnapshot(
            ConnectionState.CONNECTED,
            True,
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

    def stop(self, timeout=5.0):
        self.stopped = True
        return True


class MenuBarApplicationTests(unittest.TestCase):
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
