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

# Current English defaultMessage and Simplified Chinese locale pairs extracted
# from the named composer placeholder IDs in the Codex desktop app's app.asar.
# Exact matching prevents another ProseMirror area, such as feedback, from
# qualifying while still covering every task/conversation mode.
COMPOSER_DESCRIPTION_SOURCES = {
    "chatgptConversations.composer.placeholder": (
        "Message ChatGPT",
        "给 ChatGPT 发消息",
    ),
    "homePage.composer.placeholder.askAnything.v2": (
        "Do anything",
        "随心输入",
    ),
    "composer.placeholder.cloudFollowUp.directFollowUp": (
        "Follow up on this cloud chat",
        "跟进此云端聊天",
    ),
    "composer.placeholder.cloudFollowUp.local": (
        "Create a new local task that references this cloud task",
        "创建引用此云端任务的新的本地任务",
    ),
    "composer.placeholder.cloudFollowUp.workingTree": (
        "Create a new cloud task that includes your current code "
        "and references this task",
        "创建包含当前代码并将引用此任务的新的云端任务",
    ),
    "composer.placeholder.goal": (
        "Describe your goal, define measurable outcomes for best results",
        "描述你的目标，定义可衡量的成果，以获得最佳效果",
    ),
    "composer.placeholder.localFollowUp.cloud": (
        "Follow up in a new cloud chat",
        "在新的云端聊天中跟进",
    ),
    "composer.placeholder.localFollowUp.locally": (
        "Ask for follow-up changes",
        "提出后续变更要求",
    ),
    "composer.placeholder.localFollowUp.locallyWithAgents": (
        "Ask for follow-up changes or @ to tag an agent",
        "要求提供后续变更或 @ 提及智能体",
    ),
    "composer.placeholder.newTask.cloud": (
        "Ask Codex to do anything in the cloud",
        "在云端向 Codex 下达任意指令\u200b",
    ),
    "composer.placeholder.newTask.doAnything": (
        "Ask Codex to do anything",
        "向 Codex 下达任意指令\u200b",
    ),
    "composer.placeholder.newTask.locally.chatgptDesktop": (
        "Ask ChatGPT anything. @ to use plugins or mention files",
        "可向 ChatGPT 询问任何问题。输入 @ 使用插件或提及文件",
    ),
    "composer.placeholder.newTask.locally.v2": (
        "Ask Codex anything. @ to use plugins or mention files",
        "可向 Codex 询问任何问题。输入 @ 使用插件或提及文件",
    ),
    "composer.placeholder.plan": (
        "Describe your task to generate a plan...",
        "描述你的任务以生成套餐…",
    ),
    "composer.placeholder.workWithChatGPT": (
        "Work with ChatGPT",
        "使用 ChatGPT Work",
    ),
    "composer.placeholder.working": (
        "Working…",
        "处理中…",
    ),
}
COMPOSER_DESCRIPTIONS = frozenset(
    description
    for localized_pair in COMPOSER_DESCRIPTION_SOURCES.values()
    for description in localized_pair
)

AX_DOM_CLASS_LIST_ATTRIBUTE = "AXDOMClassList"
AX_CHROME_NODE_ID_ATTRIBUTE = "AXChromeAXNodeId"
PROSEMIRROR_CLASS = "ProseMirror"
RICH_TEXT_INPUT_CLASS_MARKER = "_RichTextInput_"
COMPOSER_BODY_CLASS_MARKER = "_ComposerLayoutBody_"
PLACEHOLDER_CLASS_MARKER = "placeholder"

DEFAULT_MAX_COMPOSER_NODES = 64
DEFAULT_MAX_COMPOSER_DESCENDANT_DEPTH = 4

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


@dataclasses.dataclass(frozen=True)
class PreparedSubmit:
    snapshot: ComposerSnapshot
    pid: int
    focused_token: Any
    original_value: str
    button_token: Any
    semantic: SubmitSemantic
    button_element: Any = dataclasses.field(compare=False, repr=False)


class AccessibilityAdapter(Protocol):
    def prepare(self) -> Optional[PreparedSubmit]:
        ...

    def normal_submit(self, prepared: PreparedSubmit) -> None:
        ...

    def alternate_submit(self, prepared: PreparedSubmit) -> None:
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
        if action not in (InputAction.SUBMIT, InputAction.FORCE_STEER):
            self._logger.info("Ignoring input action: unknown action")
            return False
        try:
            prepared = self._accessibility.prepare()
        except Exception:
            self._logger.exception(
                "Ignoring input action: Accessibility preparation failed"
            )
            return False
        if prepared is None:
            self._logger.info("Ignoring input action: no safe Codex composer")
            return False

        reason = self._unsafe_reason(prepared.snapshot)
        if reason is not None:
            self._logger.info("Ignoring input action: %s", reason)
            return False

        try:
            if action is InputAction.SUBMIT:
                self._accessibility.normal_submit(prepared)
                return True
            if prepared.semantic in (SubmitSemantic.SEND, SubmitSemantic.STEER):
                self._accessibility.normal_submit(prepared)
                return True
            if prepared.semantic is SubmitSemantic.QUEUE:
                self._accessibility.alternate_submit(prepared)
                return True
        except Exception:
            self._logger.exception("Ignoring input action: targeted dispatch failed")
            return False

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


def _perform_ax_action(element: Any, action: str) -> int:
    return int(ApplicationServices.AXUIElementPerformAction(element, action))


class MacOSCodexAccessibility:
    """Prepare, revalidate, and target the focused Codex composer."""

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
        perform_action: Callable[[Any, str], int] = _perform_ax_action,
        create_key_event: Callable[[Any, int, bool], Any] = (
            Quartz.CGEventCreateKeyboardEvent
        ),
        set_event_flags: Callable[[Any, int], None] = Quartz.CGEventSetFlags,
        post_event_to_pid: Callable[[int, Any], None] = Quartz.CGEventPostToPid,
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
        self._perform_action = perform_action
        self._create_key_event = create_key_event
        self._set_event_flags = set_event_flags
        self._post_event_to_pid = post_event_to_pid
        self._max_nodes = max_nodes
        self._max_depth = max_depth
        self._logger = logger

    def snapshot(self) -> ComposerSnapshot:
        current, _prepared = self._inspect()
        return current

    def prepare(self) -> Optional[PreparedSubmit]:
        _current, prepared = self._inspect()
        return prepared

    def _inspect(self) -> tuple[ComposerSnapshot, Optional[PreparedSubmit]]:
        foreground = False
        try:
            application = self._workspace().frontmostApplication()
            if application is None:
                return self._empty_result(False)
            foreground = (
                application.bundleIdentifier() == CODEX_BUNDLE_IDENTIFIER
            )
            if not foreground:
                return self._empty_result(False)
            if not self._trusted():
                self._logger.info(
                    "Cannot inspect Codex composer: Accessibility permission required"
                )
                return self._empty_result(True)

            pid = int(application.processIdentifier())
            ax_application = self._create_application(pid)
            focused = self._read_attribute(
                ax_application,
                ApplicationServices.kAXFocusedUIElementAttribute,
            )
            if focused is None or not self._is_codex_composer(focused):
                return self._empty_result(True)

            focused_token = self._required_token(focused)
            value = self._read_attribute(
                focused, ApplicationServices.kAXValueAttribute
            )
            if not isinstance(value, str):
                return self._empty_result(True)
            has_text = bool(value.strip()) and not self._has_placeholder(focused)

            body = self._composer_body(focused)
            if body is None:
                return self._empty_result(True)
            button_match = self._unique_semantic_button(body)
            if button_match is None:
                return (
                    ComposerSnapshot(
                        codex_foreground=True,
                        editable=True,
                        has_text=has_text,
                        semantic=SubmitSemantic.UNKNOWN,
                    ),
                    None,
                )
            button, button_token, semantic = button_match
            current = ComposerSnapshot(True, True, has_text, semantic)
            if not has_text:
                return current, None
            return current, PreparedSubmit(
                snapshot=current,
                pid=pid,
                focused_token=focused_token,
                original_value=value,
                button_token=button_token,
                semantic=semantic,
                button_element=button,
            )
        except Exception:
            self._logger.exception("Failed to inspect Codex Accessibility state")
            return self._empty_result(foreground)

    @staticmethod
    def _empty_result(
        codex_foreground: bool,
    ) -> tuple[ComposerSnapshot, Optional[PreparedSubmit]]:
        return (
            ComposerSnapshot(
                codex_foreground=codex_foreground,
                editable=False,
                has_text=False,
                semantic=SubmitSemantic.UNKNOWN,
            ),
            None,
        )

    def _is_codex_composer(self, element: Any) -> bool:
        if self._read_attribute(
            element, ApplicationServices.kAXRoleAttribute
        ) != ApplicationServices.kAXTextAreaRole:
            return False
        if self._read_attribute(
            element, ApplicationServices.kAXEnabledAttribute
        ) is not True:
            return False
        if not self._is_attribute_settable(
            element, ApplicationServices.kAXValueAttribute
        ):
            return False
        description = self._read_attribute(
            element, ApplicationServices.kAXDescriptionAttribute
        )
        if description not in COMPOSER_DESCRIPTIONS:
            return False
        return PROSEMIRROR_CLASS in self._class_tokens(element)

    def _composer_body(self, focused: Any) -> Optional[Any]:
        rich_text_input = self._read_attribute(
            focused, ApplicationServices.kAXParentAttribute
        )
        if not self._has_class_marker(
            rich_text_input, RICH_TEXT_INPUT_CLASS_MARKER
        ):
            return None
        body = self._read_attribute(
            rich_text_input, ApplicationServices.kAXParentAttribute
        )
        if not self._has_class_marker(body, COMPOSER_BODY_CLASS_MARKER):
            return None

        # ComposerLayoutRoot may wrap this body, but Chromium can omit that
        # transparent node. The verified RichTextInput -> Body chain is the
        # stable boundary used for the local button search.
        return body

    def _class_tokens(self, element: Any) -> tuple[str, ...]:
        if element is None:
            return ()
        classes = self._read_attribute(element, AX_DOM_CLASS_LIST_ATTRIBUTE)
        if isinstance(classes, str):
            return tuple(classes.split())
        if isinstance(classes, (list, tuple)):
            return tuple(value for value in classes if isinstance(value, str))
        return ()

    def _has_class_marker(self, element: Any, marker: str) -> bool:
        return any(marker in token for token in self._class_tokens(element))

    def _walk_descendants(self, root: Any):
        pending = deque(((root, 0),))
        visited = set()
        while pending and len(visited) < self._max_nodes:
            element, depth = pending.popleft()
            if element in visited:
                continue
            visited.add(element)
            yield element
            if depth >= self._max_depth:
                continue
            children = self._read_attribute(
                element, ApplicationServices.kAXChildrenAttribute
            )
            if isinstance(children, (list, tuple)):
                pending.extend((child, depth + 1) for child in children)

    def _has_placeholder(self, focused: Any) -> bool:
        for element in self._walk_descendants(focused):
            if element == focused:
                continue
            if self._has_class_marker(element, PLACEHOLDER_CLASS_MARKER):
                return True
        return False

    def _unique_semantic_button(
        self, body: Any
    ) -> Optional[tuple[Any, Any, SubmitSemantic]]:
        candidates = []
        for element in self._walk_descendants(body):
            role = self._read_attribute(
                element, ApplicationServices.kAXRoleAttribute
            )
            if role != ApplicationServices.kAXButtonRole:
                continue
            semantic = self._button_semantic(element)
            if semantic is SubmitSemantic.UNKNOWN:
                continue
            candidates.append((element, semantic))
        if len(candidates) != 1:
            return None
        button, semantic = candidates[0]
        if self._read_attribute(
            button, ApplicationServices.kAXEnabledAttribute
        ) is not True:
            return None
        return button, self._required_token(button), semantic

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

    def _required_token(self, element: Any) -> Any:
        token = self._read_attribute(element, AX_CHROME_NODE_ID_ATTRIBUTE)
        if token is None or isinstance(token, bool):
            raise RuntimeError("Accessibility element has no stable Chrome node ID")
        hash(token)
        return token

    def _revalidate(self, prepared: PreparedSubmit) -> PreparedSubmit:
        _current, fresh = self._inspect()
        if fresh is None:
            raise RuntimeError("Codex composer is no longer safe")
        stable_fields = (
            "pid",
            "focused_token",
            "original_value",
            "button_token",
            "semantic",
        )
        if any(
            getattr(fresh, field) != getattr(prepared, field)
            for field in stable_fields
        ):
            raise RuntimeError("Codex composer changed before dispatch")
        return fresh

    def normal_submit(self, prepared: PreparedSubmit) -> None:
        fresh = self._revalidate(prepared)
        error = self._perform_action(
            fresh.button_element, ApplicationServices.kAXPressAction
        )
        if error != ApplicationServices.kAXErrorSuccess:
            raise RuntimeError("Codex submit AXPress failed")

    def alternate_submit(self, prepared: PreparedSubmit) -> None:
        if prepared.semantic is not SubmitSemantic.QUEUE:
            raise RuntimeError("alternate submit requires Queue semantics")
        fresh = self._revalidate(prepared)
        self._post_modified_return_to_pid(fresh.pid)

    def _post_modified_return_to_pid(self, pid: int) -> None:
        key_down = self._create_key_event(None, RETURN_KEY_CODE, True)
        key_up = self._create_key_event(None, RETURN_KEY_CODE, False)
        if key_down is None or key_up is None:
            raise RuntimeError("could not create Return key event")
        self._set_event_flags(key_down, ALTERNATE_MODIFIER_FLAGS)
        self._set_event_flags(key_up, ALTERNATE_MODIFIER_FLAGS)
        # CGEventPostToPid prevents cross-application delivery. A same-process
        # focus change after revalidation remains an unavoidable non-atomic
        # macOS Accessibility/event boundary; callers must never retry.
        try:
            self._post_event_to_pid(pid, key_down)
        finally:
            self._post_event_to_pid(pid, key_up)
