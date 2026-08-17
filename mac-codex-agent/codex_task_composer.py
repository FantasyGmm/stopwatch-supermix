"""Route-bound Codex task submission through the active desktop composer."""

from __future__ import annotations

import asyncio
import dataclasses
import time
import urllib.parse
from collections import deque
from typing import Any, Callable, Optional

import ApplicationServices
import Quartz
from AppKit import NSWorkspace
from Foundation import NSURL


CODEX_BUNDLE_IDENTIFIER = "com.openai.codex"
CODEX_THREAD_URL_PREFIX = "codex://threads/"
RETURN_KEY_CODE = 0x24
ALTERNATE_MODIFIER_FLAGS = Quartz.kCGEventFlagMaskCommand

AX_DOM_CLASS_LIST_ATTRIBUTE = "AXDOMClassList"
AX_CHROME_NODE_ID_ATTRIBUTE = "ChromeAXNodeId"
PROSEMIRROR_CLASS = "ProseMirror"
RICH_TEXT_INPUT_CLASS_MARKER = "_RichTextInput_"
COMPOSER_BODY_CLASS_MARKER = "_ComposerLayoutBody_"
PLACEHOLDER_CLASS_MARKER = "placeholder"

COMPOSER_DESCRIPTIONS = frozenset(
    (
        "Message ChatGPT",
        "给 ChatGPT 发消息",
        "Do anything",
        "随心输入",
        "Follow up on this cloud chat",
        "跟进此云端聊天",
        "Ask for follow-up changes",
        "提出后续变更要求",
        "Ask for follow-up changes or @ to tag an agent",
        "要求提供后续变更或 @ 提及智能体",
        "Ask Codex anything. @ to use plugins or mention files",
        "可向 Codex 询问任何问题。输入 @ 使用插件或提及文件",
        "Ask ChatGPT anything. @ to use plugins or mention files",
        "可向 ChatGPT 询问任何问题。输入 @ 使用插件或提及文件",
        "Working…",
        "处理中…",
    )
)

SEND_LABELS = frozenset(("send", "发送"))
QUEUE_LABELS = frozenset(("queue", "加入队列"))
STEER_LABELS = frozenset(("steer", "调整方向"))


class TaskComposerError(RuntimeError):
    """The selected Codex composer could not be used safely."""


@dataclasses.dataclass(frozen=True)
class _ComposerTarget:
    pid: int
    composer_token: Any
    composer: Any = dataclasses.field(compare=False, repr=False)
    original_value: str = ""


@dataclasses.dataclass(frozen=True)
class _DispatchTarget:
    composer: _ComposerTarget
    semantic: str
    button_token: Any
    button: Any = dataclasses.field(compare=False, repr=False)


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
    return error == ApplicationServices.kAXErrorSuccess and bool(settable)


def _set_ax_attribute(element: Any, attribute: str, value: Any) -> int:
    return int(
        ApplicationServices.AXUIElementSetAttributeValue(
            element, attribute, value
        )
    )


def _perform_ax_action(element: Any, action: str) -> int:
    return int(ApplicationServices.AXUIElementPerformAction(element, action))


def _open_thread_url(url: str) -> bool:
    parsed = NSURL.URLWithString_(url)
    return parsed is not None and bool(
        NSWorkspace.sharedWorkspace().openURL_(parsed)
    )


class CodexTaskComposerClient:
    """Open one Codex task, write an empty composer, and dispatch once."""

    def __init__(
        self,
        *,
        open_url: Callable[[str], bool] = _open_thread_url,
        trusted: Callable[[], bool] = lambda: bool(
            ApplicationServices.AXIsProcessTrustedWithOptions(None)
        ),
        workspace: Callable[[], Any] = NSWorkspace.sharedWorkspace,
        create_application: Callable[[int], Any] = (
            ApplicationServices.AXUIElementCreateApplication
        ),
        read_attribute: Callable[[Any, str], Any] = _read_ax_attribute,
        is_attribute_settable: Callable[[Any, str], bool] = (
            _is_ax_attribute_settable
        ),
        set_attribute: Callable[[Any, str, Any], int] = _set_ax_attribute,
        perform_action: Callable[[Any, str], int] = _perform_ax_action,
        create_key_event: Callable[[Any, int, bool], Any] = (
            Quartz.CGEventCreateKeyboardEvent
        ),
        set_event_flags: Callable[[Any, int], None] = Quartz.CGEventSetFlags,
        post_event_to_pid: Callable[[int, Any], None] = Quartz.CGEventPostToPid,
        monotonic: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
        timeout: float = 2.0,
        poll_interval: float = 0.05,
        max_nodes: int = 1500,
        max_depth: int = 32,
    ) -> None:
        if timeout <= 0 or poll_interval < 0:
            raise ValueError("invalid task composer timing")
        if max_nodes < 1 or max_depth < 0:
            raise ValueError("invalid task composer traversal bound")
        self._open_url = open_url
        self._trusted = trusted
        self._workspace = workspace
        self._create_application = create_application
        self._read_attribute = read_attribute
        self._is_attribute_settable = is_attribute_settable
        self._set_attribute = set_attribute
        self._perform_action = perform_action
        self._create_key_event = create_key_event
        self._set_event_flags = set_event_flags
        self._post_event_to_pid = post_event_to_pid
        self._monotonic = monotonic
        self._sleep = sleep
        self._timeout = timeout
        self._poll_interval = poll_interval
        self._max_nodes = max_nodes
        self._max_depth = max_depth

    async def start_turn(self, thread_id: str, text: str) -> None:
        await asyncio.to_thread(self._submit_blocking, thread_id, text)

    async def steer_turn(
        self, thread_id: str, expected_turn_id: str, text: str
    ) -> None:
        if not isinstance(expected_turn_id, str) or not expected_turn_id:
            raise TaskComposerError("Codex task turn is unavailable")
        await asyncio.to_thread(self._submit_blocking, thread_id, text)

    async def close(self, timeout: float = 1.0) -> None:
        del timeout

    def _submit_blocking(self, thread_id: str, text: str) -> None:
        if not isinstance(thread_id, str) or not thread_id:
            raise TaskComposerError("Codex task is unavailable")
        if not isinstance(text, str) or not text:
            raise TaskComposerError("Voice transcript is unavailable")
        if not self._trusted():
            raise TaskComposerError("Accessibility permission is required")
        encoded = urllib.parse.quote(thread_id, safe="")
        if not self._open_url(CODEX_THREAD_URL_PREFIX + encoded):
            raise TaskComposerError("Codex task could not be opened")

        deadline = self._monotonic() + self._timeout
        composer = self._wait_for_empty_composer(deadline)
        error = self._set_attribute(
            composer.composer,
            ApplicationServices.kAXValueAttribute,
            text,
        )
        if error != ApplicationServices.kAXErrorSuccess:
            raise TaskComposerError("Codex task composer rejected the transcript")

        try:
            dispatch = self._wait_for_dispatch(composer, text, deadline)
        except Exception:
            self._clear_if_unchanged(composer, text)
            raise

        if dispatch.semantic == "queue":
            self._post_modified_return(dispatch.composer.pid)
            return
        if dispatch.semantic not in ("send", "steer"):
            self._clear_if_unchanged(composer, text)
            raise TaskComposerError("Codex task action changed before dispatch")
        error = self._perform_action(
            dispatch.button, ApplicationServices.kAXPressAction
        )
        if error != ApplicationServices.kAXErrorSuccess:
            raise TaskComposerError("Codex task dispatch was not accepted")

    def _wait_for_empty_composer(self, deadline: float) -> _ComposerTarget:
        while self._monotonic() < deadline:
            result = self._current_composer()
            if result is not None:
                value = self._read_attribute(
                    result.composer, ApplicationServices.kAXValueAttribute
                )
                if not isinstance(value, str):
                    raise TaskComposerError("Codex task composer is unavailable")
                has_placeholder = self._has_placeholder(result.composer)
                if value.strip() and not has_placeholder:
                    raise TaskComposerError("Codex task composer contains a draft")
                return dataclasses.replace(
                    result,
                    original_value="" if has_placeholder else value,
                )
            self._sleep(self._poll_interval)
        raise TaskComposerError("Codex task composer did not become ready")

    def _wait_for_dispatch(
        self, expected: _ComposerTarget, text: str, deadline: float
    ) -> _DispatchTarget:
        while self._monotonic() < deadline:
            current = self._current_composer()
            if current is not None:
                value = self._read_attribute(
                    current.composer, ApplicationServices.kAXValueAttribute
                )
                if (
                    current.pid != expected.pid
                    or current.composer_token != expected.composer_token
                    or value != text
                ):
                    raise TaskComposerError(
                        "Codex task changed before dispatch"
                    )
                button = self._unique_action_button(current.composer)
                if button is not None:
                    element, token, semantic = button
                    return _DispatchTarget(
                        current, semantic, token, element
                    )
            self._sleep(self._poll_interval)
        raise TaskComposerError("Codex task action did not become ready")

    def _current_composer(self) -> Optional[_ComposerTarget]:
        application = self._workspace().frontmostApplication()
        if (
            application is None
            or application.bundleIdentifier() != CODEX_BUNDLE_IDENTIFIER
        ):
            return None
        pid = int(application.processIdentifier())
        root = self._create_application(pid)
        candidates = [
            element
            for element in self._walk(root, self._max_nodes, self._max_depth)
            if self._is_composer(element)
        ]
        if len(candidates) != 1:
            return None
        composer = candidates[0]
        token = self._required_token(composer)
        return _ComposerTarget(pid, token, composer)

    def _is_composer(self, element: Any) -> bool:
        return (
            self._read_attribute(
                element, ApplicationServices.kAXRoleAttribute
            )
            == ApplicationServices.kAXTextAreaRole
            and self._read_attribute(
                element, ApplicationServices.kAXEnabledAttribute
            )
            is True
            and self._read_attribute(
                element, ApplicationServices.kAXHiddenAttribute
            )
            is not True
            and self._read_attribute(
                element, ApplicationServices.kAXDescriptionAttribute
            )
            in COMPOSER_DESCRIPTIONS
            and PROSEMIRROR_CLASS in self._class_tokens(element)
            and self._is_attribute_settable(
                element, ApplicationServices.kAXValueAttribute
            )
            and self._composer_body(element) is not None
        )

    def _composer_body(self, composer: Any) -> Optional[Any]:
        rich = self._read_attribute(
            composer, ApplicationServices.kAXParentAttribute
        )
        if not self._has_class_marker(rich, RICH_TEXT_INPUT_CLASS_MARKER):
            return None
        body = self._read_attribute(rich, ApplicationServices.kAXParentAttribute)
        if not self._has_class_marker(body, COMPOSER_BODY_CLASS_MARKER):
            return None
        return body

    def _unique_action_button(
        self, composer: Any
    ) -> Optional[tuple[Any, Any, str]]:
        body = self._composer_body(composer)
        matches = []
        for element in self._walk(body, 128, 8):
            if (
                self._read_attribute(
                    element, ApplicationServices.kAXRoleAttribute
                )
                != ApplicationServices.kAXButtonRole
                or self._read_attribute(
                    element, ApplicationServices.kAXEnabledAttribute
                )
                is not True
                or self._read_attribute(
                    element, ApplicationServices.kAXHiddenAttribute
                )
                is True
            ):
                continue
            semantic = self._button_semantic(element)
            if semantic is not None:
                matches.append(
                    (element, self._required_token(element), semantic)
                )
        return matches[0] if len(matches) == 1 else None

    def _button_semantic(self, element: Any) -> Optional[str]:
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
                matches.add("send")
            elif normalized in QUEUE_LABELS:
                matches.add("queue")
            elif normalized in STEER_LABELS:
                matches.add("steer")
        return next(iter(matches)) if len(matches) == 1 else None

    def _has_placeholder(self, composer: Any) -> bool:
        return any(
            element != composer
            and self._has_class_marker(element, PLACEHOLDER_CLASS_MARKER)
            for element in self._walk(composer, 64, 4)
        )

    def _clear_if_unchanged(self, expected: _ComposerTarget, text: str) -> None:
        try:
            current = self._current_composer()
            if (
                current is not None
                and current.pid == expected.pid
                and current.composer_token == expected.composer_token
                and self._read_attribute(
                    current.composer,
                    ApplicationServices.kAXValueAttribute,
                )
                == text
            ):
                self._set_attribute(
                    current.composer,
                    ApplicationServices.kAXValueAttribute,
                    expected.original_value,
                )
        except Exception:
            return

    def _post_modified_return(self, pid: int) -> None:
        key_down = self._create_key_event(None, RETURN_KEY_CODE, True)
        key_up = self._create_key_event(None, RETURN_KEY_CODE, False)
        if key_down is None or key_up is None:
            raise TaskComposerError("Codex task key event is unavailable")
        self._set_event_flags(key_down, ALTERNATE_MODIFIER_FLAGS)
        self._set_event_flags(key_up, ALTERNATE_MODIFIER_FLAGS)
        try:
            self._post_event_to_pid(pid, key_down)
        finally:
            self._post_event_to_pid(pid, key_up)

    def _required_token(self, element: Any) -> Any:
        token = self._read_attribute(element, AX_CHROME_NODE_ID_ATTRIBUTE)
        if token is None or isinstance(token, bool):
            raise TaskComposerError("Codex task element has no stable identity")
        hash(token)
        return token

    def _class_tokens(self, element: Any) -> tuple[str, ...]:
        if element is None:
            return ()
        classes = self._read_attribute(element, AX_DOM_CLASS_LIST_ATTRIBUTE)
        if isinstance(classes, str):
            return tuple(classes.split())
        return tuple(
            value
            for value in self._collection_values(classes)
            if isinstance(value, str)
        )

    def _has_class_marker(self, element: Any, marker: str) -> bool:
        return any(marker in token for token in self._class_tokens(element))

    @staticmethod
    def _collection_values(value: Any) -> tuple[Any, ...]:
        if isinstance(value, (str, bytes, bytearray)):
            return ()
        try:
            return tuple(iter(value))
        except (TypeError, RuntimeError):
            return ()

    def _walk(self, root: Any, max_nodes: int, max_depth: int):
        if root is None:
            return
        pending = deque(((root, 0),))
        visited = set()
        while pending and len(visited) < max_nodes:
            element, depth = pending.popleft()
            if element in visited:
                continue
            visited.add(element)
            yield element
            if depth >= max_depth:
                continue
            children = self._read_attribute(
                element, ApplicationServices.kAXChildrenAttribute
            )
            pending.extend(
                (child, depth + 1)
                for child in self._collection_values(children)
            )
