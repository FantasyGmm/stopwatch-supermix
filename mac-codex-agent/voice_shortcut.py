"""Accessibility-gated Fn shortcut control for an already-running Typeless."""

from __future__ import annotations

import time
from collections.abc import Callable

import ApplicationServices
import Quartz
from AppKit import NSRunningApplication


TYPELESS_BUNDLE_IDENTIFIER = "now.typeless.desktop"
FN_KEY_CODE = 0x3F


class AccessibilityRequired(RuntimeError):
    """Raised when macOS Accessibility access has not been granted."""


class TypelessNotRunning(RuntimeError):
    """Raised when Typeless is not already running."""


def _is_accessibility_trusted() -> bool:
    return bool(ApplicationServices.AXIsProcessTrustedWithOptions(None))


def _is_typeless_running() -> bool:
    applications = NSRunningApplication.runningApplicationsWithBundleIdentifier_(
        TYPELESS_BUNDLE_IDENTIFIER
    )
    return bool(applications)


def _post_fn(down: bool) -> None:
    event = Quartz.CGEventCreateKeyboardEvent(None, FN_KEY_CODE, down)
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, event)


class FnShortcutController:
    def __init__(
        self,
        trusted: Callable[[], bool] = _is_accessibility_trusted,
        typeless_running: Callable[[], bool] = _is_typeless_running,
        post_key: Callable[[bool], None] = _post_fn,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        self._trusted = trusted
        self._typeless_running = typeless_running
        self._post_key = post_key
        self._sleep = sleep
        self.active = False

    def _tap(self) -> None:
        try:
            self._post_key(True)
            self._sleep(0.05)
        finally:
            self._post_key(False)

    def start(self) -> None:
        if not self._trusted():
            raise AccessibilityRequired()
        if not self._typeless_running():
            raise TypelessNotRunning()
        self._tap()
        self.active = True

    def stop(self) -> None:
        if not self.active:
            return
        self.active = False
        self._tap()
