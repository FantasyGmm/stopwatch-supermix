"""Native macOS menu bar UI for the StopWatch Codex agent."""

from __future__ import annotations

import logging
import logging.handlers
import pathlib
import threading
import time
from typing import Optional

import objc
from AppKit import (
    NSAlert,
    NSAlertFirstButtonReturn,
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
    NSTextAlignmentRight,
    NSTextField,
    NSVariableStatusItemLength,
    NSView,
    NSWorkspace,
)
from Foundation import NSBundle, NSTimer, NSURL
from PyObjCTools import AppHelper

from app_state import UserDefaultsPreferences, present
from pet_window import CodexPetWindowController
from runtime import AgentController
from voice_driver import (
    BLACKHOLE_PACKAGE_NAME,
    BlackHoleInstaller,
    DriverInstallCancelled,
    DriverInstallError,
    DriverState,
)
from voice_session import VoiceState


LOG_PATH = pathlib.Path.home() / "Library" / "Logs" / "stopwatch-codex-agent.log"
LOGGER = logging.getLogger(__name__)
_delegate = None
STATUS_ROW_WIDTH = 240
STATUS_ROW_HEIGHT = 24
STATUS_HEADER_HEIGHT = 27
STATUS_LEFT_MARGIN = 18
STATUS_CHILD_INDENT = 16
ALERT_ROW_WIDTH = 360
INSTALL_SHUTDOWN_TIMEOUT = 1.0
ROOT_MENU_WIDTH = 430
ROOT_HEALTH_HEIGHT = 61
ROOT_SOURCE_HEIGHT = 52
ROOT_DEVICE_HEIGHT = 40
ROOT_ACTION_HEIGHT = 27
ROOT_PRIMARY_COLOR = (0xED, 0xF1, 0xF7)
ROOT_SECONDARY_COLOR = (0x8C, 0x94, 0xA2)
ROOT_GREEN_COLOR = (0x5B, 0xD6, 0x9B)
ROOT_ORANGE_COLOR = (0xFF, 0xB3, 0x5A)
ROOT_BLUE_COLOR = (0x69, 0xA7, 0xFF)
ROOT_BADGE_TEXT_COLOR = (0xB5, 0xD1, 0xFF)
ROOT_BADGE_BACKGROUND_COLOR = (0x25, 0x34, 0x4A)


def bundled_agent_version() -> str:
    bundle = NSBundle.mainBundle()
    value = bundle.objectForInfoDictionaryKey_("CFBundleShortVersionString")
    if bundle.bundleIdentifier() == "com.stopwatch.codex-agent" and isinstance(
        value, str
    ):
        parts = value.strip().split(".")
        if len(parts) == 3 and all(
            part.isascii() and part.isdigit() for part in parts
        ):
            return value.strip()
    try:
        value = pathlib.Path(__file__).with_name("VERSION").read_text(
            encoding="utf-8"
        ).strip()
    except OSError:
        return "dev"
    parts = value.split(".")
    return (
        value
        if len(parts) == 3
        and all(part.isascii() and part.isdigit() for part in parts)
        else "dev"
    )


def bundled_blackhole_package() -> pathlib.Path:
    resource_path = NSBundle.mainBundle().resourcePath()
    if not resource_path:
        resource_path = pathlib.Path(__file__).resolve().parent
    return pathlib.Path(resource_path) / "Drivers" / BLACKHOLE_PACKAGE_NAME


def confirm_voice_setup(alert_factory=None) -> bool:
    alert = alert_factory() if alert_factory is not None else NSAlert.alloc().init()
    alert.setMessageText_("Set Up Watch Voice Input?")
    alert.setInformativeText_(
        "BlackHole requires administrator authorization. "
        "A Mac restart may be required."
    )
    alert.addButtonWithTitle_("Install")
    alert.addButtonWithTitle_("Cancel")
    return alert.runModal() == NSAlertFirstButtonReturn


def confirm_forget_watch(alert_factory=None) -> bool:
    alert = alert_factory() if alert_factory is not None else NSAlert.alloc().init()
    alert.setMessageText_("Forget This Watch Credential?")
    alert.setInformativeText_(
        "The Agent will reconnect and require the six-digit code plus "
        "physical approval on the watch before receiving a new credential."
    )
    alert.addButtonWithTitle_("Forget and Re-pair")
    alert.addButtonWithTitle_("Cancel")
    return alert.runModal() == NSAlertFirstButtonReturn


def prompt_agent_name(current_name: str, alert_factory=None, field_factory=None):
    alert = alert_factory() if alert_factory is not None else NSAlert.alloc().init()
    field = (
        field_factory()
        if field_factory is not None
        else NSTextField.alloc().initWithFrame_(((0, 0), (280, 24)))
    )
    field.setStringValue_(current_name)
    alert.setMessageText_("Name This Agent")
    alert.setInformativeText_(
        "This name is signed by this Mac and shown in the watch Agent list."
    )
    alert.setAccessoryView_(field)
    alert.addButtonWithTitle_("Save")
    alert.addButtonWithTitle_("Cancel")
    if alert.runModal() != NSAlertFirstButtonReturn:
        return None
    return str(field.stringValue())


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
        self.workspace = NSWorkspace.sharedWorkspace()
        self.voice_installer = BlackHoleInstaller()
        self.voice_package_path = bundled_blackhole_package()
        self.voice_setup_confirmer = confirm_voice_setup
        self.watch_forget_confirmer = confirm_forget_watch
        self.agent_name_prompt = prompt_agent_name
        self.voice_worker_factory = lambda target: threading.Thread(
            target=target,
            name="blackhole-installer",
            daemon=True,
        )
        self.call_after = AppHelper.callAfter
        self._voice_install_thread = None
        self._voice_setup_token = None
        self.codex_pet_window_controller = None
        self.pet_window_controller_factory = CodexPetWindowController
        self._lifecycle_lock = threading.Lock()
        self._stopping = False
        self._installing_voice = False
        self._last_alert_revision = None
        self._alert_labels = []
        self.agent_version = bundled_agent_version()
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
        self.menu.setMinimumWidth_(ROOT_MENU_WIDTH)
        (
            self.agent_health_item,
            self.agent_health_view,
            self.agent_health_dot,
            self.agent_health_title_label,
            self.agent_health_subtitle_label,
            self.agent_version_label,
        ) = self._root_health_item()
        self.agent_health_label = self.agent_health_subtitle_label
        self.menu.addItem_(NSMenuItem.separatorItem())

        (
            self.codex_item,
            self.codex_submenu,
            self.codex_summary_view,
            self.codex_summary_dot,
            self.codex_summary_title_label,
            self.codex_summary_subtitle_label,
            self.codex_summary_status_label,
            self.codex_summary_detail_label,
        ) = self._root_source_item(
            "Codex", self._root_color(ROOT_ORANGE_COLOR)
        )
        self.codex_header_item, self.codex_header_label = self._informational_item(
            "Codex Status", header=True, menu=self.codex_submenu
        )
        self.model_item, self.model_label = self._informational_item(
            "Model: --", indent=STATUS_CHILD_INDENT, menu=self.codex_submenu
        )
        self.effort_item, self.effort_label = self._informational_item(
            "Effort: --", indent=STATUS_CHILD_INDENT, menu=self.codex_submenu
        )
        self.five_hour_item, self.five_hour_label = self._informational_item(
            "5H Remaining: --", indent=STATUS_CHILD_INDENT, menu=self.codex_submenu
        )
        self.seven_day_item, self.seven_day_label = self._informational_item(
            "7D Remaining: --", indent=STATUS_CHILD_INDENT, menu=self.codex_submenu
        )
        self.codex_submenu.addItem_(NSMenuItem.separatorItem())
        self.alert_item = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_(
            "Task Alerts · 0 Pending / 0 Running / 0 Completed", None, ""
        )
        self.alert_item.setEnabled_(True)
        self.alert_submenu = NSMenu.alloc().init()
        self.alert_submenu.setAutoenablesItems_(False)
        self.alert_item.setSubmenu_(self.alert_submenu)
        self.codex_submenu.addItem_(self.alert_item)

        (
            self.trae_item,
            self.trae_submenu,
            self.trae_summary_view,
            self.trae_summary_dot,
            self.trae_summary_title_label,
            self.trae_summary_subtitle_label,
            self.trae_summary_status_label,
            self.trae_summary_detail_label,
        ) = self._root_source_item("Trae", self._root_color(ROOT_BLUE_COLOR))
        self.trae_header_item, self.trae_header_label = self._informational_item(
            "Trae Status", header=True, menu=self.trae_submenu
        )
        self.trae_online_item, self.trae_online_label = self._informational_item(
            "Source: Offline", indent=STATUS_CHILD_INDENT, menu=self.trae_submenu
        )
        self.trae_model_item, self.trae_model_label = self._informational_item(
            "Model: --", indent=STATUS_CHILD_INDENT, menu=self.trae_submenu
        )
        self.trae_effort_item, self.trae_effort_label = self._informational_item(
            "Effort: --", indent=STATUS_CHILD_INDENT, menu=self.trae_submenu
        )
        self.trae_project_item, self.trae_project_label = self._informational_item(
            "Project: --", indent=STATUS_CHILD_INDENT, menu=self.trae_submenu
        )
        self.trae_state_item, self.trae_state_label = self._informational_item(
            "State: --", indent=STATUS_CHILD_INDENT, menu=self.trae_submenu
        )
        self.trae_task_item, self.trae_task_label = self._informational_item(
            "Task: --", indent=STATUS_CHILD_INDENT, menu=self.trae_submenu
        )
        self.trae_detail_item, self.trae_detail_label = self._informational_item(
            "Detail: --", indent=STATUS_CHILD_INDENT, menu=self.trae_submenu
        )

        self.menu.addItem_(NSMenuItem.separatorItem())
        (
            self.device_voice_item,
            self.device_submenu,
            self.device_summary_view,
            self.device_summary_dot,
            self.device_summary_title_label,
            self.device_summary_subtitle_label,
        ) = self._root_device_item(
            self._root_color(ROOT_GREEN_COLOR)
        )
        (
            self.connection_header_item,
            self.connection_header_label,
        ) = self._informational_item(
            "Connection Status", header=True, menu=self.device_submenu
        )
        self.agent_item, self.agent_label = self._informational_item(
            "Agent: Running", indent=STATUS_CHILD_INDENT, menu=self.device_submenu
        )
        self.pairing_item, self.pairing_label = self._informational_item(
            "Pairing: Idle", indent=STATUS_CHILD_INDENT, menu=self.device_submenu
        )
        self.verification_item, self.verification_label = self._informational_item(
            "Verification Code: --",
            indent=STATUS_CHILD_INDENT,
            menu=self.device_submenu,
        )
        self.watch_identity_item, self.watch_identity_label = (
            self._informational_item(
                "Watch: --", indent=STATUS_CHILD_INDENT, menu=self.device_submenu
            )
        )
        self.bluetooth_item, self.bluetooth_label = self._informational_item(
            "Bluetooth: Scanning",
            indent=STATUS_CHILD_INDENT,
            menu=self.device_submenu,
        )
        self.device_item, self.device_label = self._informational_item(
            "Device: StopWatch-Codex",
            indent=STATUS_CHILD_INDENT,
            menu=self.device_submenu,
        )
        self.voice_item, self.voice_label = self._informational_item(
            "Voice Input: Error",
            indent=STATUS_CHILD_INDENT,
            menu=self.device_submenu,
        )
        self.setup_voice_item = self._action_item(
            "Set Up Watch Voice Input...", "setupVoice:", root=True
        )
        self.accessibility_item = self._action_item(
            "Open Accessibility Settings...",
            "openAccessibilitySettings:",
            root=True,
        )
        self.codex_pet_item = self._action_item("Codex Pet…", "openCodexPet:")
        self.menu.addItem_(NSMenuItem.separatorItem())
        self.reconnect_item = self._action_item(
            "立即重新连接", "reconnect:", root=True
        )

        self.settings_item, self.settings_submenu = self._root_submenu_item(
            "Agent 设置"
        )
        self.connection_item = self._action_item(
            "Enable Watch Connection",
            "toggleConnection:",
            menu=self.settings_submenu,
        )
        self.rename_agent_item = self._action_item(
            "Rename Agent...", "renameAgent:", menu=self.settings_submenu
        )
        self.retry_pairing_item = self._action_item(
            "Retry Pairing", "retryPairing:", root=True
        )
        self.forget_watch_item = self._action_item(
            "Forget This Watch Credential...", "forgetWatch:", root=True
        )

        self.diagnostics_item, self.diagnostics_submenu = self._root_submenu_item(
            "诊断"
        )
        self.open_log_item = self._action_item(
            "Open Log", "openLog:", menu=self.diagnostics_submenu
        )
        self.menu.addItem_(NSMenuItem.separatorItem())
        self.quit_item = self._action_item(
            "退出 StopWatch Codex Agent", "quitApplication:", root=True
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

    @staticmethod
    def _root_color(rgb):
        return NSColor.colorWithSRGBRed_green_blue_alpha_(
            rgb[0] / 255.0,
            rgb[1] / 255.0,
            rgb[2] / 255.0,
            1.0,
        )

    def _root_label(
        self,
        title,
        frame,
        size,
        color=None,
        semibold=False,
        alignment=None,
    ):
        label = NSTextField.labelWithString_(title)
        label.setFrame_(frame)
        label.setFont_(
            NSFont.systemFontOfSize_weight_(size, NSFontWeightSemibold)
            if semibold
            else NSFont.systemFontOfSize_(size)
        )
        label.setTextColor_(
            color if color is not None else self._root_color(ROOT_PRIMARY_COLOR)
        )
        label.setLineBreakMode_(NSLineBreakByTruncatingTail)
        label.setUsesSingleLineMode_(True)
        if alignment is not None:
            label.setAlignment_(alignment)
        return label

    @staticmethod
    def _set_dot_color(dot, color):
        dot.layer().setBackgroundColor_(color.CGColor())

    def _root_dot(self, frame, color):
        dot = NSView.alloc().initWithFrame_(frame)
        dot.setWantsLayer_(True)
        dot.layer().setCornerRadius_(3.5)
        self._set_dot_color(dot, color)
        return dot

    def _root_health_item(self):
        view = NSView.alloc().initWithFrame_(
            ((0, 0), (ROOT_MENU_WIDTH, ROOT_HEALTH_HEIGHT))
        )
        dot = self._root_dot(
            ((12, 40), (7, 7)), self._root_color(ROOT_GREEN_COLOR)
        )
        title = self._root_label(
            "StopWatch 启动中",
            ((27, 31), (315, 22)),
            14,
            semibold=True,
        )
        subtitle = self._root_label(
            "Agent · 语音状态检查中",
            ((27, 8), (335, 17)),
            11,
            color=self._root_color(ROOT_SECONDARY_COLOR),
        )
        version = self._root_label(
            "v{}".format(self.agent_version),
            ((366, 32), (52, 19)),
            11,
            color=self._root_color(ROOT_BADGE_TEXT_COLOR),
            alignment=NSTextAlignmentRight,
        )
        version.setWantsLayer_(True)
        version.layer().setBackgroundColor_(
            self._root_color(ROOT_BADGE_BACKGROUND_COLOR).CGColor()
        )
        version.layer().setCornerRadius_(9.5)
        for child in (dot, title, subtitle, version):
            view.addSubview_(child)
        item = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_(
            "Agent Status", None, ""
        )
        item.setEnabled_(False)
        item.setView_(view)
        self.menu.addItem_(item)
        return item, view, dot, title, subtitle, version

    def _root_source_item(self, source, dot_color):
        view = NSView.alloc().initWithFrame_(
            ((0, 0), (ROOT_MENU_WIDTH, ROOT_SOURCE_HEIGHT))
        )
        dot = self._root_dot(((12, 23), (7, 7)), dot_color)
        title = self._root_label(
            source, ((27, 29), (225, 17)), 13, semibold=True
        )
        subtitle = self._root_label(
            "--",
            ((27, 10), (240, 15)),
            11,
            color=self._root_color(ROOT_SECONDARY_COLOR),
        )
        status = self._root_label(
            "--",
            ((270, 29), (128, 16)),
            13,
            alignment=NSTextAlignmentRight,
        )
        detail = self._root_label(
            "--",
            ((260, 10), (138, 14)),
            10,
            color=self._root_color(ROOT_SECONDARY_COLOR),
            alignment=NSTextAlignmentRight,
        )
        chevron = self._root_label(
            "›",
            ((407, 14), (11, 24)),
            18,
            color=self._root_color(ROOT_SECONDARY_COLOR),
            alignment=NSTextAlignmentRight,
        )
        for child in (dot, title, subtitle, status, detail, chevron):
            view.addSubview_(child)
        item = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_(
            source, None, ""
        )
        item.setEnabled_(True)
        submenu = NSMenu.alloc().init()
        submenu.setAutoenablesItems_(False)
        item.setSubmenu_(submenu)
        item.setView_(view)
        self.menu.addItem_(item)
        return item, submenu, view, dot, title, subtitle, status, detail

    def _root_device_item(self, dot_color):
        view = NSView.alloc().initWithFrame_(
            ((0, 0), (ROOT_MENU_WIDTH, ROOT_DEVICE_HEIGHT))
        )
        dot = self._root_dot(((12, 17), (7, 7)), dot_color)
        title = self._root_label(
            "设备与语音", ((27, 20), (255, 16)), 13, semibold=True
        )
        subtitle = self._root_label(
            "--",
            ((27, 4), (340, 14)),
            11,
            color=self._root_color(ROOT_SECONDARY_COLOR),
        )
        chevron = self._root_label(
            "›",
            ((407, 8), (11, 24)),
            18,
            color=self._root_color(ROOT_SECONDARY_COLOR),
            alignment=NSTextAlignmentRight,
        )
        for child in (dot, title, subtitle, chevron):
            view.addSubview_(child)
        item = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_(
            "设备与语音", None, ""
        )
        item.setEnabled_(True)
        submenu = NSMenu.alloc().init()
        submenu.setAutoenablesItems_(False)
        item.setSubmenu_(submenu)
        item.setView_(view)
        self.menu.addItem_(item)
        return item, submenu, view, dot, title, subtitle

    def _style_root_action_item(self, item, title, chevron=False):
        view = NSView.alloc().initWithFrame_(
            ((0, 0), (ROOT_MENU_WIDTH, ROOT_ACTION_HEIGHT))
        )
        label = self._root_label(title, ((12, 4), (385, 19)), 13)
        view.addSubview_(label)
        if chevron:
            arrow = self._root_label(
                "›",
                ((407, 2), (11, 23)),
                18,
                color=self._root_color(ROOT_SECONDARY_COLOR),
                alignment=NSTextAlignmentRight,
            )
            view.addSubview_(arrow)
        item.setView_(view)
        return view, label

    def _root_submenu_item(self, title):
        item = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_(title, None, "")
        item.setEnabled_(True)
        submenu = NSMenu.alloc().init()
        submenu.setAutoenablesItems_(False)
        item.setSubmenu_(submenu)
        self._style_root_action_item(item, title, chevron=True)
        self.menu.addItem_(item)
        return item, submenu

    def _submenu_item(self, title):
        item = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_(title, None, "")
        item.setEnabled_(True)
        submenu = NSMenu.alloc().init()
        submenu.setAutoenablesItems_(False)
        item.setSubmenu_(submenu)
        self.menu.addItem_(item)
        return item, submenu

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

    def _action_item(self, title, action, menu=None, root=False):
        item = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_(title, action, "")
        item.setTarget_(self)
        target_menu = menu if menu is not None else self.menu
        if root:
            self._style_root_action_item(item, title)
        target_menu.addItem_(item)
        return item

    def refresh_(self, _timer):
        view = present(self.controller.snapshot())
        self.agent_health_title_label.setStringValue_(view.root_health_title)
        self.agent_health_subtitle_label.setStringValue_(view.root_health_subtitle)
        self.codex_summary_title_label.setStringValue_(view.root_codex_title)
        self.codex_summary_subtitle_label.setStringValue_(view.root_codex_subtitle)
        self.codex_summary_status_label.setStringValue_(view.root_codex_status)
        self.codex_summary_detail_label.setStringValue_(view.root_codex_detail)
        self.trae_summary_title_label.setStringValue_(view.root_trae_title)
        self.trae_summary_subtitle_label.setStringValue_(view.root_trae_subtitle)
        self.trae_summary_status_label.setStringValue_(view.root_trae_status)
        self.trae_summary_detail_label.setStringValue_(view.root_trae_detail)
        self.device_summary_title_label.setStringValue_(view.root_device_title)
        self.device_summary_subtitle_label.setStringValue_(view.root_device_subtitle)
        connected = "Connected" in view.bluetooth_text
        ready = view.voice_state in (VoiceState.READY, VoiceState.RECORDING)
        state_color = self._root_color(
            ROOT_GREEN_COLOR if connected else ROOT_SECONDARY_COLOR
        )
        self._set_dot_color(self.agent_health_dot, state_color)
        self._set_dot_color(
            self.device_summary_dot,
            self._root_color(
                ROOT_GREEN_COLOR if connected and ready else ROOT_SECONDARY_COLOR
            ),
        )
        self.codex_summary_status_label.setTextColor_(
            self._root_color(
                ROOT_ORANGE_COLOR
                if view.root_codex_status != "0 Pending"
                else ROOT_SECONDARY_COLOR
            )
        )
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
        self.pairing_label.setStringValue_(view.pairing_text)
        self.verification_label.setStringValue_(view.verification_text)
        self.watch_identity_label.setStringValue_(view.watch_identity_text)
        self.bluetooth_label.setStringValue_(view.bluetooth_text)
        self.device_label.setStringValue_(view.device_text)
        self.voice_label.setStringValue_(view.voice_text)
        setup_visible = view.voice_state is VoiceState.DRIVER_REQUIRED
        accessibility_visible = (
            view.voice_state is VoiceState.ACCESSIBILITY_REQUIRED
        )
        self.setup_voice_item.setHidden_(not setup_visible)
        self.setup_voice_item.setEnabled_(
            setup_visible and not self._installing_voice and not self._stopping
        )
        self.accessibility_item.setHidden_(not accessibility_visible)
        self.accessibility_item.setEnabled_(
            accessibility_visible and not self._stopping
        )
        self.connection_item.setState_(
            NSControlStateValueOn if view.connection_checked else NSControlStateValueOff
        )
        self.reconnect_item.setEnabled_(view.reconnect_enabled and not self._stopping)
        self.connection_item.setEnabled_(not self._stopping)
        self.rename_agent_item.setEnabled_(not self._stopping)
        pairing_failed = view.pairing_text == "Pairing: Failed"
        self.retry_pairing_item.setHidden_(not pairing_failed)
        self.retry_pairing_item.setEnabled_(pairing_failed and not self._stopping)
        self.forget_watch_item.setHidden_(not pairing_failed)
        self.forget_watch_item.setEnabled_(pairing_failed and not self._stopping)
        image = NSImage.imageWithSystemSymbolName_accessibilityDescription_(
            view.symbol_name,
            view.bluetooth_text,
        )
        if image is not None:
            image.setTemplate_(True)
            self.status_item.button().setImage_(image)
        if self.codex_pet_window_controller is not None:
            self.codex_pet_window_controller.applySnapshotAndPreview(
                self.controller.pet_snapshot(), self.controller.pet_preview()
            )

    def toggleConnection_(self, sender):
        enabled = sender.state() != NSControlStateValueOn
        self.controller.set_connection_enabled(enabled)

    def reconnect_(self, _sender):
        self.controller.reconnect()

    def retryPairing_(self, _sender):
        self.controller.reconnect()

    def forgetWatch_(self, _sender):
        if not self.watch_forget_confirmer():
            return
        if not self.controller.forget_current_watch():
            LOGGER.warning("No current watch credential was available to forget")
        self.refresh_(None)

    def renameAgent_(self, _sender):
        current_name = self.controller.snapshot().agent_name
        requested = self.agent_name_prompt(current_name)
        if requested is None:
            return
        try:
            self.controller.rename_agent(requested)
        except (ValueError, RuntimeError) as error:
            LOGGER.warning("Could not rename Agent: %s", error)
            alert = NSAlert.alloc().init()
            alert.setMessageText_("Agent Name Was Not Changed")
            alert.setInformativeText_(str(error))
            alert.runModal()
        self.refresh_(None)

    def setupVoice_(self, _sender):
        if self._installing_voice or self._stopping:
            return
        if (
            present(self.controller.snapshot()).voice_state
            is not VoiceState.DRIVER_REQUIRED
        ):
            return
        if not self.voice_setup_confirmer():
            return
        setup_token = self.controller.begin_voice_setup()
        if setup_token is None:
            self.refresh_(None)
            return
        self._installing_voice = True
        self._voice_setup_token = setup_token
        self.setup_voice_item.setEnabled_(False)
        worker = self.voice_worker_factory(self._install_voice)
        self._voice_install_thread = worker
        worker.start()

    def _install_voice(self):
        setup_token = self._voice_setup_token
        try:
            state = self.voice_installer.install(self.voice_package_path)
        except DriverInstallCancelled as error:
            self._schedule_voice_setup_completion(
                DriverState.ERROR,
                str(error),
                setup_token,
            )
        except DriverInstallError as error:
            LOGGER.error("BlackHole installation failed: %s", error)
            self._schedule_voice_setup_completion(
                DriverState.ERROR,
                str(error),
                setup_token,
            )
        except Exception:
            LOGGER.exception("BlackHole installation failed")
            self._schedule_voice_setup_completion(
                DriverState.ERROR,
                "Driver setup failed",
                setup_token,
            )
        else:
            self._schedule_voice_setup_completion(state, "", setup_token)

    def _schedule_voice_setup_completion(
        self, state, detail="", setup_token=None
    ) -> bool:
        with self._lifecycle_lock:
            if self._stopping:
                return False
            self.call_after(
                self._finish_voice_setup,
                state,
                detail,
                setup_token,
            )
            return True

    def _finish_voice_setup(self, state, detail="", setup_token=None):
        self._installing_voice = False
        self._voice_install_thread = None
        self._voice_setup_token = None
        if self._stopping:
            return
        if state is DriverState.ERROR:
            self.controller.report_voice_setup_failure(setup_token, detail)
        if state in (DriverState.READY, DriverState.RESTART_REQUIRED):
            self.controller.report_voice_setup_result(setup_token, state)
            self.controller.refresh_voice_readiness()
        self.refresh_(None)

    def openAccessibilitySettings_(self, _sender):
        url = NSURL.URLWithString_(
            "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"
        )
        self.workspace.openURL_(url)

    def openCodexPet_(self, sender):
        if self._stopping:
            return
        if self.codex_pet_window_controller is None:
            self.codex_pet_window_controller = (
                self.pet_window_controller_factory.alloc().initWithAgentController_(
                    self.controller
                )
            )
        window_controller = self.codex_pet_window_controller
        window_controller.applySnapshotAndPreview(
            self.controller.pet_snapshot(), self.controller.pet_preview()
        )
        window_controller.showWindow_(sender)
        window_controller.window().makeKeyAndOrderFront_(sender)

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
        with self._lifecycle_lock:
            if self._stopping:
                return
            self._stopping = True
        if self.timer is not None:
            self.timer.invalidate()
            self.timer = None
        self.connection_item.setEnabled_(False)
        self.reconnect_item.setEnabled_(False)
        self.rename_agent_item.setEnabled_(False)
        self.retry_pairing_item.setEnabled_(False)
        self.forget_watch_item.setEnabled_(False)
        self.setup_voice_item.setEnabled_(False)
        self.accessibility_item.setEnabled_(False)
        self.codex_pet_item.setEnabled_(False)
        if self.codex_pet_window_controller is not None:
            self.codex_pet_window_controller.stopPreviewTimer()
        worker = self._voice_install_thread
        deadline = time.monotonic() + INSTALL_SHUTDOWN_TIMEOUT
        close_installer = getattr(self.voice_installer, "close", None)
        reaped = True
        if close_installer is not None:
            try:
                reaped = close_installer(timeout=INSTALL_SHUTDOWN_TIMEOUT / 2)
            except Exception:
                reaped = False
                LOGGER.exception("Could not cancel BlackHole installation")
        if worker is not None and worker.is_alive():
            worker.join(max(0.0, deadline - time.monotonic()))
        if close_installer is not None:
            try:
                reaped = close_installer(timeout=0.0)
            except Exception:
                reaped = False
                LOGGER.exception("Could not confirm BlackHole installer shutdown")
        worker_alive = worker is not None and worker.is_alive()
        if not worker_alive:
            self._voice_install_thread = None
        if worker_alive or not reaped:
            LOGGER.error("BlackHole installer did not stop within one second")
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
