"""Accessibility-gated control using Typeless' configured shortcut."""

from __future__ import annotations

import json
import time
from collections.abc import Callable
from pathlib import Path

import ApplicationServices
import Quartz
from AppKit import NSRunningApplication


TYPELESS_BUNDLE_IDENTIFIER = "now.typeless.desktop"
TYPELESS_SETTINGS_PATH = (
    Path.home() / "Library" / "Application Support" / "Typeless"
    / "app-settings.json"
)

# Typeless stores macOS shortcut tokens rather than Quartz virtual key codes.
# These are the hardware-independent key positions used by CGEvent.
TYPELESS_KEY_CODES = {
    "A": 0x00,
    "S": 0x01,
    "D": 0x02,
    "F": 0x03,
    "H": 0x04,
    "G": 0x05,
    "Z": 0x06,
    "X": 0x07,
    "C": 0x08,
    "V": 0x09,
    "Section": 0x0A,
    "B": 0x0B,
    "Q": 0x0C,
    "W": 0x0D,
    "E": 0x0E,
    "R": 0x0F,
    "Y": 0x10,
    "T": 0x11,
    "1": 0x12,
    "2": 0x13,
    "3": 0x14,
    "4": 0x15,
    "6": 0x16,
    "5": 0x17,
    "=": 0x18,
    "9": 0x19,
    "7": 0x1A,
    "-": 0x1B,
    "8": 0x1C,
    "0": 0x1D,
    "]": 0x1E,
    "O": 0x1F,
    "U": 0x20,
    "[": 0x21,
    "I": 0x22,
    "P": 0x23,
    "Enter": 0x24,
    "L": 0x25,
    "J": 0x26,
    "'": 0x27,
    "K": 0x28,
    ";": 0x29,
    "\\": 0x2A,
    "Backslash2": 0x2A,
    ",": 0x2B,
    "/": 0x2C,
    "N": 0x2D,
    "M": 0x2E,
    ".": 0x2F,
    "Tab": 0x30,
    "Space": 0x31,
    "`": 0x32,
    "Backspace": 0x33,
    "Delete": 0x33,
    "Escape": 0x35,
    "LeftCmd": 0x37,
    "LeftShift": 0x38,
    "CapsLock": 0x39,
    "LeftOption": 0x3A,
    "LeftCtrl": 0x3B,
    "RightShift": 0x3C,
    "RightOption": 0x3D,
    "RightCtrl": 0x3E,
    "Fn": 0x3F,
    "F17": 0x40,
    "NumpadDecimal": 0x41,
    "NumpadMultiply": 0x43,
    "NumpadAdd": 0x45,
    "NumpadClear": 0x47,
    "VolumeUp": 0x48,
    "VolumeDown": 0x49,
    "Mute": 0x4A,
    "NumpadDivide": 0x4B,
    "NumpadEnter": 0x4C,
    "NumpadSubtract": 0x4E,
    "F18": 0x4F,
    "F19": 0x50,
    "NumpadEquals": 0x51,
    "Numpad0": 0x52,
    "Numpad1": 0x53,
    "Numpad2": 0x54,
    "Numpad3": 0x55,
    "Numpad4": 0x56,
    "Numpad5": 0x57,
    "Numpad6": 0x58,
    "Numpad7": 0x59,
    "F20": 0x5A,
    "Numpad8": 0x5B,
    "Numpad9": 0x5C,
    "F5": 0x60,
    "F6": 0x61,
    "F7": 0x62,
    "F3": 0x63,
    "F8": 0x64,
    "F9": 0x65,
    "Eisu": 0x66,
    "F11": 0x67,
    "Kana": 0x68,
    "F13": 0x69,
    "F16": 0x6A,
    "F14": 0x6B,
    "F10": 0x6D,
    "ContextMenu": 0x6E,
    "F12": 0x6F,
    "F15": 0x71,
    "Help": 0x72,
    "Home": 0x73,
    "PageUp": 0x74,
    "ForwardDelete": 0x75,
    "F4": 0x76,
    "End": 0x77,
    "F2": 0x78,
    "PageDown": 0x79,
    "F1": 0x7A,
    "Left": 0x7B,
    "Right": 0x7C,
    "Down": 0x7D,
    "Up": 0x7E,
}


class AccessibilityRequired(RuntimeError):
    """Raised when macOS Accessibility access has not been granted."""


class TypelessNotRunning(RuntimeError):
    """Raised when Typeless is not already running."""


class TypelessShortcutUnavailable(RuntimeError):
    """Raised when Typeless' configured dictation shortcut is unusable."""


def _is_accessibility_trusted() -> bool:
    return bool(ApplicationServices.AXIsProcessTrustedWithOptions(None))


def _is_typeless_running() -> bool:
    applications = NSRunningApplication.runningApplicationsWithBundleIdentifier_(
        TYPELESS_BUNDLE_IDENTIFIER
    )
    return bool(applications)


def _load_typeless_shortcut(path: Path) -> tuple[int, ...]:
    try:
        settings = json.loads(path.read_text(encoding="utf-8"))
        bindings = settings["featureShortcutBindings"]["dictationMode"]
        shortcut = bindings[0]
    except (
        OSError,
        UnicodeError,
        json.JSONDecodeError,
        KeyError,
        IndexError,
        TypeError,
    ):
        raise TypelessShortcutUnavailable(
            "Typeless dictation shortcut could not be read"
        ) from None
    if not isinstance(shortcut, str) or not shortcut:
        raise TypelessShortcutUnavailable(
            "Typeless dictation shortcut is not configured"
        )
    tokens = shortcut.split("+")
    if not 1 <= len(tokens) <= 8 or any(not token for token in tokens):
        raise TypelessShortcutUnavailable(
            "Typeless dictation shortcut has an invalid shape"
        )
    try:
        key_codes = tuple(TYPELESS_KEY_CODES[token] for token in tokens)
    except KeyError as error:
        raise TypelessShortcutUnavailable(
            f"Unsupported Typeless shortcut key: {error.args[0]}"
        ) from None
    if len(set(key_codes)) != len(key_codes):
        raise TypelessShortcutUnavailable(
            "Typeless dictation shortcut repeats a key"
        )
    return key_codes


def _load_startup_shortcut():
    try:
        return _load_typeless_shortcut(TYPELESS_SETTINGS_PATH), None
    except TypelessShortcutUnavailable as error:
        return None, error


_STARTUP_SHORTCUT, _STARTUP_SHORTCUT_ERROR = _load_startup_shortcut()


def _post_key(key_code: int, down: bool) -> None:
    event = Quartz.CGEventCreateKeyboardEvent(None, key_code, down)
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, event)


class FnShortcutController:
    def __init__(
        self,
        trusted: Callable[[], bool] = _is_accessibility_trusted,
        typeless_running: Callable[[], bool] = _is_typeless_running,
        post_key: Callable[[int, bool], None] = _post_key,
        sleep: Callable[[float], None] = time.sleep,
        settings_path: Path | None = None,
    ) -> None:
        self._trusted = trusted
        self._typeless_running = typeless_running
        self._post_key = post_key
        self._sleep = sleep
        if settings_path is None:
            self._shortcut = _STARTUP_SHORTCUT
            self._shortcut_error = _STARTUP_SHORTCUT_ERROR
        else:
            try:
                self._shortcut = _load_typeless_shortcut(settings_path)
                self._shortcut_error = None
            except TypelessShortcutUnavailable as error:
                self._shortcut = None
                self._shortcut_error = error
        self.active = False

    def _tap(self) -> None:
        if self._shortcut is None:
            raise TypelessShortcutUnavailable(str(self._shortcut_error))
        pressed = []
        failure = None
        try:
            for key_code in self._shortcut:
                self._post_key(key_code, True)
                pressed.append(key_code)
            self._sleep(0.05)
        except Exception as error:
            failure = error
        finally:
            for key_code in reversed(pressed):
                try:
                    self._post_key(key_code, False)
                except Exception as error:
                    if failure is None:
                        failure = error
        if failure is not None:
            raise failure

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
