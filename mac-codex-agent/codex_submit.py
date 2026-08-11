"""Fail-closed Codex composer submission through macOS Accessibility."""

from __future__ import annotations

import dataclasses
import enum
import logging
from collections import deque
from typing import Any, Callable, Optional, Protocol

import ApplicationServices
import Quartz
from AppKit import NSWorkspace

from input_action_protocol import InputAction


CODEX_BUNDLE_IDENTIFIER = "com.openai.codex"
RETURN_KEY_CODE = 0x24
ALTERNATE_MODIFIER_FLAGS = (
    Quartz.kCGEventFlagMaskCommand | Quartz.kCGEventFlagMaskShift
)

SEND_LABELS = frozenset(("send", "发送"))
QUEUE_LABELS = frozenset(("queue", "加入队列"))
STEER_LABELS = frozenset(("steer", "调整方向"))

NEAREST_COMPOSER_CONTAINER_PARENT_HOPS = 1
DEFAULT_MAX_COMPOSER_NODES = 64
DEFAULT_MAX_COMPOSER_DESCENDANT_DEPTH = 2
COMPOSER_CONTAINER_ROLES = frozenset((ApplicationServices.kAXGroupRole,))

_LOGGER = logging.getLogger(__name__)


class SubmitSemantic(enum.Enum):
    SEND = "send"
    QUEUE = "queue"
    STEER = "steer"
    UNKNOWN = "unknown"


@dataclasses.dataclass(frozen=True)
class ComposerSnapshot:
    codex_foreground: bool
    editable: bool
    has_text: bool
    semantic: SubmitSemantic


class AccessibilityAdapter(Protocol):
    def snapshot(self) -> ComposerSnapshot:
        ...

    def normal_submit(self) -> None:
        ...

    def alternate_submit(self) -> None:
        ...


class CodexSubmitController:
    def __init__(
        self,
        accessibility: AccessibilityAdapter,
        logger: logging.Logger = _LOGGER,
    ) -> None:
        self._accessibility = accessibility
        self._logger = logger

    def handle(self, action: InputAction) -> bool:
        try:
            current = self._accessibility.snapshot()
        except Exception:
            self._logger.exception(
                "Ignoring input action: Accessibility snapshot failed"
            )
            return False

        reason = self._unsafe_reason(current)
        if reason is not None:
            self._logger.info("Ignoring input action: %s", reason)
            return False

        try:
            if action is InputAction.SUBMIT:
                self._accessibility.normal_submit()
                return True
            if action is not InputAction.FORCE_STEER:
                self._logger.info("Ignoring input action: unknown action")
                return False
            if current.semantic in (SubmitSemantic.SEND, SubmitSemantic.STEER):
                self._accessibility.normal_submit()
                return True
            if current.semantic is SubmitSemantic.QUEUE:
                self._accessibility.alternate_submit()
                return True
        except Exception:
            self._logger.exception("Ignoring input action: keyboard dispatch failed")
            return False

        self._logger.info("Ignoring input action: unknown submit semantics")
        return False

    @staticmethod
    def _unsafe_reason(current: ComposerSnapshot) -> Optional[str]:
        if not isinstance(current, ComposerSnapshot):
            return "Accessibility snapshot is malformed"
        if not current.codex_foreground:
            return "Codex is not foreground"
        if not current.editable:
            return "focused Codex composer is not editable"
        if not current.has_text:
            return "focused Codex composer is empty"
        if current.semantic not in (
            SubmitSemantic.SEND,
            SubmitSemantic.QUEUE,
            SubmitSemantic.STEER,
        ):
            return "submit semantics are unknown"
        return None


def _workspace() -> Any:
    return NSWorkspace.sharedWorkspace()


def _is_accessibility_trusted() -> bool:
    return bool(ApplicationServices.AXIsProcessTrustedWithOptions(None))


def _read_ax_attribute(element: Any, attribute: str) -> Any:
    error, value = ApplicationServices.AXUIElementCopyAttributeValue(
        element, attribute, None
    )
    if error != ApplicationServices.kAXErrorSuccess:
        return None
    return value


def _is_ax_attribute_settable(element: Any, attribute: str) -> bool:
    error, settable = ApplicationServices.AXUIElementIsAttributeSettable(
        element, attribute, None
    )
    if error != ApplicationServices.kAXErrorSuccess:
        return False
    return bool(settable)


class MacOSCodexAccessibility:
    """Read the focused Codex composer and synthesize qualified key taps."""

    def __init__(
        self,
        *,
        trusted: Callable[[], bool] = _is_accessibility_trusted,
        workspace: Callable[[], Any] = _workspace,
        create_application: Callable[[int], Any] = (
            ApplicationServices.AXUIElementCreateApplication
        ),
        read_attribute: Callable[[Any, str], Any] = _read_ax_attribute,
        is_attribute_settable: Callable[[Any, str], bool] = (
            _is_ax_attribute_settable
        ),
        create_key_event: Callable[[Any, int, bool], Any] = (
            Quartz.CGEventCreateKeyboardEvent
        ),
        set_event_flags: Callable[[Any, int], None] = Quartz.CGEventSetFlags,
        post_event: Callable[[int, Any], None] = Quartz.CGEventPost,
        max_nodes: int = DEFAULT_MAX_COMPOSER_NODES,
        max_depth: int = DEFAULT_MAX_COMPOSER_DESCENDANT_DEPTH,
        logger: logging.Logger = _LOGGER,
    ) -> None:
        if max_nodes < 1:
            raise ValueError("max_nodes must be positive")
        if max_depth < 0:
            raise ValueError("max_depth must not be negative")
        self._trusted = trusted
        self._workspace = workspace
        self._create_application = create_application
        self._read_attribute = read_attribute
        self._is_attribute_settable = is_attribute_settable
        self._create_key_event = create_key_event
        self._set_event_flags = set_event_flags
        self._post_event = post_event
        self._max_nodes = max_nodes
        self._max_depth = max_depth
        self._logger = logger

    def snapshot(self) -> ComposerSnapshot:
        foreground = False
        try:
            application = self._workspace().frontmostApplication()
            if application is None:
                return self._empty_snapshot(False)
            foreground = (
                application.bundleIdentifier() == CODEX_BUNDLE_IDENTIFIER
            )
            if not foreground:
                return self._empty_snapshot(False)
            if not self._trusted():
                self._logger.info(
                    "Cannot inspect Codex composer: Accessibility permission required"
                )
                return self._empty_snapshot(True)

            ax_application = self._create_application(
                int(application.processIdentifier())
            )
            focused = self._read_attribute(
                ax_application,
                ApplicationServices.kAXFocusedUIElementAttribute,
            )
            if focused is None or not self._is_editable_text(focused):
                return self._empty_snapshot(True)

            value = self._read_attribute(
                focused, ApplicationServices.kAXValueAttribute
            )
            has_text = isinstance(value, str) and bool(value.strip())
            return ComposerSnapshot(
                codex_foreground=True,
                editable=True,
                has_text=has_text,
                semantic=self._nearby_submit_semantic(focused),
            )
        except Exception:
            self._logger.exception("Failed to inspect Codex Accessibility state")
            return self._empty_snapshot(foreground)

    @staticmethod
    def _empty_snapshot(codex_foreground: bool) -> ComposerSnapshot:
        return ComposerSnapshot(
            codex_foreground=codex_foreground,
            editable=False,
            has_text=False,
            semantic=SubmitSemantic.UNKNOWN,
        )

    def _is_editable_text(self, element: Any) -> bool:
        role = self._read_attribute(element, ApplicationServices.kAXRoleAttribute)
        if role not in (
            ApplicationServices.kAXTextAreaRole,
            ApplicationServices.kAXTextFieldRole,
        ):
            return False
        if self._read_attribute(
            element, ApplicationServices.kAXEnabledAttribute
        ) is not True:
            return False
        return self._is_attribute_settable(
            element, ApplicationServices.kAXValueAttribute
        )

    def _nearby_submit_semantic(self, focused: Any) -> SubmitSemantic:
        container = focused
        for _ in range(NEAREST_COMPOSER_CONTAINER_PARENT_HOPS):
            container = self._read_attribute(
                container, ApplicationServices.kAXParentAttribute
            )
            if container is None:
                return SubmitSemantic.UNKNOWN
        container_role = self._read_attribute(
            container, ApplicationServices.kAXRoleAttribute
        )
        if container_role not in COMPOSER_CONTAINER_ROLES:
            return SubmitSemantic.UNKNOWN

        semantics = set()
        pending = deque(((container, 0),))
        visited = set()
        while pending and len(visited) < self._max_nodes:
            element, depth = pending.popleft()
            identity = id(element)
            if identity in visited:
                continue
            visited.add(identity)

            role = self._read_attribute(
                element, ApplicationServices.kAXRoleAttribute
            )
            if role == ApplicationServices.kAXButtonRole:
                semantic = self._button_semantic(element)
                if semantic is not SubmitSemantic.UNKNOWN:
                    semantics.add(semantic)
                    if len(semantics) > 1:
                        return SubmitSemantic.UNKNOWN

            if depth >= self._max_depth:
                continue
            children = self._read_attribute(
                element, ApplicationServices.kAXChildrenAttribute
            )
            if isinstance(children, (list, tuple)):
                pending.extend((child, depth + 1) for child in children)

        if len(semantics) == 1:
            return next(iter(semantics))
        return SubmitSemantic.UNKNOWN

    def _button_semantic(self, element: Any) -> SubmitSemantic:
        matches = set()
        for attribute in (
            ApplicationServices.kAXTitleAttribute,
            ApplicationServices.kAXDescriptionAttribute,
            ApplicationServices.kAXHelpAttribute,
        ):
            value = self._read_attribute(element, attribute)
            if not isinstance(value, str):
                continue
            normalized = " ".join(value.casefold().split())
            if normalized in SEND_LABELS:
                matches.add(SubmitSemantic.SEND)
            elif normalized in QUEUE_LABELS:
                matches.add(SubmitSemantic.QUEUE)
            elif normalized in STEER_LABELS:
                matches.add(SubmitSemantic.STEER)
        if len(matches) == 1:
            return next(iter(matches))
        return SubmitSemantic.UNKNOWN

    def normal_submit(self) -> None:
        self._post_return(0)

    def alternate_submit(self) -> None:
        self._post_return(ALTERNATE_MODIFIER_FLAGS)

    def _post_return(self, flags: int) -> None:
        key_down = self._create_key_event(None, RETURN_KEY_CODE, True)
        key_up = self._create_key_event(None, RETURN_KEY_CODE, False)
        if key_down is None or key_up is None:
            raise RuntimeError("could not create Return key event")
        if flags:
            self._set_event_flags(key_down, flags)
            self._set_event_flags(key_up, flags)
        try:
            self._post_event(Quartz.kCGHIDEventTap, key_down)
        finally:
            self._post_event(Quartz.kCGHIDEventTap, key_up)
