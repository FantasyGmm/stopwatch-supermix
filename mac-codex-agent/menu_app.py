"""Native macOS menu bar UI for the StopWatch Codex agent."""

from __future__ import annotations

import logging
import logging.handlers
import pathlib
from typing import Optional

import objc
from AppKit import (
    NSApplication,
    NSApplicationActivationPolicyAccessory,
    NSColor,
    NSControlStateValueOff,
    NSControlStateValueOn,
    NSFont,
    NSFontWeightSemibold,
    NSImage,
    NSImageLeading,
    NSLineBreakByTruncatingTail,
    NSMenu,
    NSMenuItem,
    NSObject,
    NSStatusBar,
    NSTextField,
    NSVariableStatusItemLength,
    NSView,
    NSWorkspace,
)
from Foundation import NSTimer, NSURL

from app_state import UserDefaultsPreferences, present
from runtime import AgentController


LOG_PATH = pathlib.Path.home() / "Library" / "Logs" / "stopwatch-codex-agent.log"
LOGGER = logging.getLogger(__name__)
_delegate = None
STATUS_ROW_WIDTH = 240
STATUS_ROW_HEIGHT = 24
STATUS_HEADER_HEIGHT = 27
STATUS_LEFT_MARGIN = 18
STATUS_CHILD_INDENT = 16
ALERT_ROW_WIDTH = 360


def configure_logging(path: pathlib.Path = LOG_PATH) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    handler = logging.handlers.RotatingFileHandler(
        path,
        maxBytes=1_000_000,
        backupCount=2,
        encoding="utf-8",
    )
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        handlers=[handler],
        force=True,
    )


class MenuBarApplication(NSObject):
    def initWithController_(self, controller):
        self = objc.super(MenuBarApplication, self).init()
        if self is None:
            return None
        self.controller = controller
        self.status_item = None
        self.menu = None
        self.timer: Optional[NSTimer] = None
        self._stopping = False
        self._last_alert_revision = None
        self._alert_labels = []
        return self

    def applicationDidFinishLaunching_(self, _notification):
        self.status_item = NSStatusBar.systemStatusBar().statusItemWithLength_(
            NSVariableStatusItemLength
        )
        button = self.status_item.button()
        button.setToolTip_("StopWatch Codex Agent")
        button.setTitle_(" Codex")
        button.setImagePosition_(NSImageLeading)

        self.menu = NSMenu.alloc().init()
        self.menu.setAutoenablesItems_(False)
        self.codex_header_item, self.codex_header_label = self._informational_item(
            "Codex Status", header=True
        )
        self.model_item, self.model_label = self._informational_item(
            "Model: --", indent=STATUS_CHILD_INDENT
        )
        self.effort_item, self.effort_label = self._informational_item(
            "Effort: --", indent=STATUS_CHILD_INDENT
        )
        self.five_hour_item, self.five_hour_label = self._informational_item(
            "5H Remaining: --", indent=STATUS_CHILD_INDENT
        )
        self.seven_day_item, self.seven_day_label = self._informational_item(
            "7D Remaining: --", indent=STATUS_CHILD_INDENT
        )
        self.alert_item = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_(
            "Task Alerts · 0 Pending / 0 Running / 0 Completed", None, ""
        )
        self.alert_item.setEnabled_(True)
        self.alert_submenu = NSMenu.alloc().init()
        self.alert_submenu.setAutoenablesItems_(False)
        self.alert_item.setSubmenu_(self.alert_submenu)
        self.menu.addItem_(self.alert_item)
        self.menu.addItem_(NSMenuItem.separatorItem())
        self.trae_header_item, self.trae_header_label = self._informational_item(
            "Trae Status", header=True
        )
        self.trae_online_item, self.trae_online_label = self._informational_item(
            "Source: Offline", indent=STATUS_CHILD_INDENT
        )
        self.trae_model_item, self.trae_model_label = self._informational_item(
            "Model: --", indent=STATUS_CHILD_INDENT
        )
        self.trae_effort_item, self.trae_effort_label = self._informational_item(
            "Effort: --", indent=STATUS_CHILD_INDENT
        )
        self.trae_project_item, self.trae_project_label = self._informational_item(
            "Project: --", indent=STATUS_CHILD_INDENT
        )
        self.trae_state_item, self.trae_state_label = self._informational_item(
            "State: --", indent=STATUS_CHILD_INDENT
        )
        self.trae_task_item, self.trae_task_label = self._informational_item(
            "Task: --", indent=STATUS_CHILD_INDENT
        )
        self.trae_detail_item, self.trae_detail_label = self._informational_item(
            "Detail: --", indent=STATUS_CHILD_INDENT
        )
        self.menu.addItem_(NSMenuItem.separatorItem())
        (
            self.connection_header_item,
            self.connection_header_label,
        ) = self._informational_item("Connection Status", header=True)
        self.agent_item, self.agent_label = self._informational_item(
            "Agent: Running", indent=STATUS_CHILD_INDENT
        )
        self.bluetooth_item, self.bluetooth_label = self._informational_item(
            "Bluetooth: Scanning", indent=STATUS_CHILD_INDENT
        )
        self.device_item, self.device_label = self._informational_item(
            "Device: StopWatch-Codex", indent=STATUS_CHILD_INDENT
        )
        self.menu.addItem_(NSMenuItem.separatorItem())
        self.connection_item = self._action_item(
            "Enable Watch Connection", "toggleConnection:"
        )
        self.reconnect_item = self._action_item("Reconnect Now", "reconnect:")
        self.menu.addItem_(NSMenuItem.separatorItem())
        self.open_log_item = self._action_item("Open Log", "openLog:")
        self.menu.addItem_(NSMenuItem.separatorItem())
        self.quit_item = self._action_item(
            "Quit StopWatch Codex Agent", "quitApplication:"
        )
        self.status_item.setMenu_(self.menu)

        self.controller.start()
        self.refresh_(None)
        self.timer = NSTimer.scheduledTimerWithTimeInterval_target_selector_userInfo_repeats_(
            0.5,
            self,
            "refresh:",
            None,
            True,
        )
        LOGGER.info(
            "StopWatch Codex Agent started; menu visible=%s width=%.0f title=%r",
            self.status_item.isVisible(),
            self.status_item.button().frame().size.width,
            self.status_item.button().title(),
        )

    def _informational_item(
        self, title, indent=0, header=False, menu=None, color=None, width=STATUS_ROW_WIDTH
    ):
        target_menu = menu if menu is not None else self.menu
        height = STATUS_HEADER_HEIGHT if header else STATUS_ROW_HEIGHT
        item = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_("", None, "")
        item.setEnabled_(False)
        view = NSView.alloc().initWithFrame_(((0, 0), (width, height)))
        label = NSTextField.labelWithString_(title)
        label.setFrame_(
            (
                (STATUS_LEFT_MARGIN + indent, 2),
                (width - STATUS_LEFT_MARGIN - indent - 8, height - 4),
            )
        )
        label.setFont_(
            NSFont.systemFontOfSize_weight_(
                NSFont.systemFontSize(), NSFontWeightSemibold
            )
            if header
            else NSFont.menuFontOfSize_(0)
        )
        label.setTextColor_(color if color is not None else NSColor.labelColor())
        label.setLineBreakMode_(NSLineBreakByTruncatingTail)
        label.setUsesSingleLineMode_(True)
        view.addSubview_(label)
        item.setView_(view)
        target_menu.addItem_(item)
        return item, label

    def _rebuild_alert_submenu(self, alerts):
        self.alert_submenu.removeAllItems()
        self._alert_labels = []
        groups = (
            ("Pending", "pending", NSColor.systemOrangeColor()),
            ("Running", "running", NSColor.systemBlueColor()),
            ("Completed", "completed", NSColor.secondaryLabelColor()),
        )
        for heading, state, color in groups:
            rows = [alert for alert in alerts if alert.state == state]
            if not rows:
                continue
            _item, label = self._informational_item(
                heading, header=True, menu=self.alert_submenu, width=ALERT_ROW_WIDTH
            )
            self._alert_labels.append(label)
            for alert in rows:
                outcome = {
                    "pending": "Waiting for Instruction",
                    "running": "Running",
                    "completed": "Completed",
                }[alert.state]
                _item, label = self._informational_item(
                    "{} · {} · {} · {}".format(
                        alert.project, outcome, alert.title, alert.time
                    ),
                    indent=STATUS_CHILD_INDENT,
                    menu=self.alert_submenu,
                    color=color,
                    width=ALERT_ROW_WIDTH,
                )
                self._alert_labels.append(label)
        if not self._alert_labels:
            _item, label = self._informational_item(
                "No alerts today",
                menu=self.alert_submenu,
                color=NSColor.secondaryLabelColor(),
                width=ALERT_ROW_WIDTH,
            )
            self._alert_labels.append(label)

    def _action_item(self, title, action):
        item = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_(title, action, "")
        item.setTarget_(self)
        self.menu.addItem_(item)
        return item

    def refresh_(self, _timer):
        view = present(self.controller.snapshot())
        self.model_label.setStringValue_(view.model_text)
        self.effort_label.setStringValue_(view.effort_text)
        self.five_hour_label.setStringValue_(view.five_hour_text)
        self.seven_day_label.setStringValue_(view.seven_day_text)
        self.alert_item.setTitle_(view.alert_summary_text)
        if self._last_alert_revision != view.alert_revision:
            self._rebuild_alert_submenu(view.alerts)
            self._last_alert_revision = view.alert_revision
        self.trae_online_label.setStringValue_(view.trae_online_text)
        self.trae_model_label.setStringValue_(view.trae_model_text)
        self.trae_effort_label.setStringValue_(view.trae_effort_text)
        self.trae_project_label.setStringValue_(view.trae_project_text)
        self.trae_state_label.setStringValue_(view.trae_state_text)
        self.trae_task_label.setStringValue_(view.trae_task_text)
        self.trae_detail_label.setStringValue_(view.trae_detail_text)
        self.agent_label.setStringValue_(view.agent_text)
        self.bluetooth_label.setStringValue_(view.bluetooth_text)
        self.device_label.setStringValue_(view.device_text)
        self.connection_item.setState_(
            NSControlStateValueOn if view.connection_checked else NSControlStateValueOff
        )
        self.reconnect_item.setEnabled_(view.reconnect_enabled and not self._stopping)
        self.connection_item.setEnabled_(not self._stopping)
        image = NSImage.imageWithSystemSymbolName_accessibilityDescription_(
            view.symbol_name,
            view.bluetooth_text,
        )
        if image is not None:
            image.setTemplate_(True)
            self.status_item.button().setImage_(image)

    def toggleConnection_(self, sender):
        enabled = sender.state() != NSControlStateValueOn
        self.controller.set_connection_enabled(enabled)

    def reconnect_(self, _sender):
        self.controller.reconnect()

    def openLog_(self, _sender):
        LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
        LOG_PATH.touch(exist_ok=True)
        NSWorkspace.sharedWorkspace().openURL_(NSURL.fileURLWithPath_(str(LOG_PATH)))

    def quitApplication_(self, _sender):
        self._shutdown()
        NSApplication.sharedApplication().terminate_(self)

    def applicationWillTerminate_(self, _notification):
        self._shutdown()

    def _shutdown(self):
        if self._stopping:
            return
        self._stopping = True
        if self.timer is not None:
            self.timer.invalidate()
            self.timer = None
        self.connection_item.setEnabled_(False)
        self.reconnect_item.setEnabled_(False)
        stopped = self.controller.stop(timeout=5.0)
        if stopped:
            LOGGER.info("StopWatch Codex Agent stopped")
        else:
            LOGGER.error("Agent worker did not stop within five seconds")
        if self.status_item is not None:
            NSStatusBar.systemStatusBar().removeStatusItem_(self.status_item)
            self.status_item = None


def main() -> None:
    global _delegate

    configure_logging()
    preferences = UserDefaultsPreferences()
    controller = AgentController(preferences=preferences)
    application = NSApplication.sharedApplication()
    application.setActivationPolicy_(NSApplicationActivationPolicyAccessory)
    _delegate = MenuBarApplication.alloc().initWithController_(controller)
    application.setDelegate_(_delegate)
    application.run()


if __name__ == "__main__":
    main()
