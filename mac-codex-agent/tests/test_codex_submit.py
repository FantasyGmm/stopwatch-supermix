import pathlib
import sys
import unittest
from types import SimpleNamespace


AGENT_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AGENT_ROOT))

from codex_submit import (  # noqa: E402
    ALTERNATE_MODIFIER_FLAGS,
    AX_CHROME_NODE_ID_ATTRIBUTE,
    CODEX_BUNDLE_IDENTIFIER,
    COMPOSER_DESCRIPTIONS,
    COMPOSER_DESCRIPTION_SOURCES,
    RETURN_KEY_CODE,
    CodexSubmitController,
    ComposerSnapshot,
    MacOSCodexAccessibility,
    SubmitSemantic,
)
from input_action_protocol import InputAction  # noqa: E402


def snapshot(
    semantic=SubmitSemantic.SEND,
    *,
    codex_foreground=True,
    editable=True,
    has_text=True,
):
    return ComposerSnapshot(
        codex_foreground=codex_foreground,
        editable=editable,
        has_text=has_text,
        semantic=semantic,
    )


class FakeAccessibility:
    def __init__(self, current_snapshot):
        self.current_snapshot = current_snapshot
        self.normal_calls = 0
        self.alternate_calls = 0
        self.dispatch_error = None

    def prepare(self):
        if isinstance(self.current_snapshot, BaseException):
            raise self.current_snapshot
        return SimpleNamespace(
            snapshot=self.current_snapshot,
            semantic=getattr(
                self.current_snapshot, "semantic", SubmitSemantic.UNKNOWN
            ),
        )

    def normal_submit(self, _prepared):
        if self.dispatch_error is not None:
            raise self.dispatch_error
        self.normal_calls += 1

    def alternate_submit(self, _prepared):
        if self.dispatch_error is not None:
            raise self.dispatch_error
        self.alternate_calls += 1


class CodexSubmitControllerTests(unittest.TestCase):
    def dispatch(self, action, current_snapshot):
        accessibility = FakeAccessibility(current_snapshot)
        handled = CodexSubmitController(accessibility).handle(action)
        return handled, accessibility

    def test_submit_uses_normal_action_for_each_recognized_semantic(self):
        for semantic in (
            SubmitSemantic.SEND,
            SubmitSemantic.QUEUE,
            SubmitSemantic.STEER,
        ):
            with self.subTest(semantic=semantic):
                handled, accessibility = self.dispatch(
                    InputAction.SUBMIT, snapshot(semantic)
                )
                self.assertTrue(handled)
                self.assertEqual(accessibility.normal_calls, 1)
                self.assertEqual(accessibility.alternate_calls, 0)

    def test_force_steer_uses_normal_for_send_and_steer_but_alternate_for_queue(self):
        for semantic, expected in (
            (SubmitSemantic.SEND, (1, 0)),
            (SubmitSemantic.STEER, (1, 0)),
            (SubmitSemantic.QUEUE, (0, 1)),
        ):
            with self.subTest(semantic=semantic):
                handled, accessibility = self.dispatch(
                    InputAction.FORCE_STEER, snapshot(semantic)
                )
                self.assertTrue(handled)
                self.assertEqual(
                    (accessibility.normal_calls, accessibility.alternate_calls),
                    expected,
                )

    def test_unsafe_malformed_unknown_and_errors_fail_closed(self):
        cases = (
            snapshot(codex_foreground=False),
            snapshot(editable=False),
            snapshot(has_text=False),
            snapshot(SubmitSemantic.UNKNOWN),
            ComposerSnapshot(True, True, True, object()),
            object(),
            RuntimeError("AX unavailable"),
        )
        for current in cases:
            with self.subTest(current=current):
                handled, accessibility = self.dispatch(InputAction.SUBMIT, current)
                self.assertFalse(handled)
                self.assertEqual(accessibility.normal_calls, 0)
                self.assertEqual(accessibility.alternate_calls, 0)

    def test_unknown_action_and_dispatch_error_fail_closed(self):
        handled, accessibility = self.dispatch(object(), snapshot())
        self.assertFalse(handled)
        self.assertEqual(accessibility.normal_calls, 0)

        accessibility = FakeAccessibility(snapshot())
        accessibility.dispatch_error = RuntimeError("dispatch failed")
        self.assertFalse(
            CodexSubmitController(accessibility).handle(InputAction.SUBMIT)
        )


class FakeApplication:
    def __init__(self, bundle_identifier=CODEX_BUNDLE_IDENTIFIER, pid=42):
        self._bundle_identifier = bundle_identifier
        self._pid = pid

    def bundleIdentifier(self):
        return self._bundle_identifier

    def processIdentifier(self):
        return self._pid


class SequencedWorkspace:
    def __init__(self, *applications):
        self.applications = applications
        self.calls = 0

    def frontmostApplication(self):
        application = self.applications[
            min(self.calls, len(self.applications) - 1)
        ]
        self.calls += 1
        return application


class Node:
    """An AX wrapper whose equality/hash represent the underlying AX node."""

    def __init__(self, token):
        self.token = token

    def __eq__(self, other):
        return isinstance(other, Node) and self.token == other.token

    def __hash__(self):
        return hash(self.token)


class NSArrayLike:
    """PyObjC-style iterable that is deliberately not a list or tuple."""

    def __init__(self, values):
        self._values = tuple(values)

    def __iter__(self):
        return iter(self._values)


class ExplodingNSArrayLike:
    def __iter__(self):
        raise RuntimeError("bridged collection failed during iteration")


class RealComposerFixture:
    def __init__(self, *, description="Do anything", label="Send", value="hello"):
        self.application_element = Node("application")
        self.focused = Node("focused")
        self.rich_text_input = Node("rich-text-input")
        self.composer_body = Node("composer-body")
        self.composer_root = Node("composer-root")
        self.button = Node("button")
        self.attributes = {
            (self.application_element, "AXFocusedUIElement"): self.focused,
            (self.focused, "AXRole"): "AXTextArea",
            (self.focused, "AXEnabled"): True,
            (self.focused, "AXDescription"): description,
            (self.focused, "AXDOMClassList"): ("ProseMirror",),
            (self.focused, "ChromeAXNodeId"): 101,
            (self.focused, "AXValue"): value,
            (self.focused, "AXParent"): self.rich_text_input,
            (self.focused, "AXChildren"): (),
            (self.rich_text_input, "AXRole"): "AXGroup",
            (self.rich_text_input, "AXDOMClassList"): (
                "_RichTextInput_abcd_1",
            ),
            (self.rich_text_input, "AXChildren"): (self.focused,),
            (self.rich_text_input, "AXParent"): self.composer_body,
            (self.composer_body, "AXRole"): "AXGroup",
            (self.composer_body, "AXDOMClassList"): (
                "_ComposerLayoutBody_abcd_1",
            ),
            (self.composer_body, "AXChildren"): (
                self.rich_text_input,
                self.button,
            ),
            (self.composer_body, "AXParent"): self.composer_root,
            (self.composer_root, "AXRole"): "AXGroup",
            (self.composer_root, "AXDOMClassList"): (
                "_ComposerLayoutRoot_abcd_1",
            ),
            (self.composer_root, "AXChildren"): (self.composer_body,),
            (self.button, "AXRole"): "AXButton",
            (self.button, "AXEnabled"): True,
            (self.button, "AXTitle"): "",
            (self.button, "AXDescription"): label,
            (self.button, "ChromeAXNodeId"): 202,
        }
        self.create_calls = 0
        self.before_revalidation = None
        self.actions = []
        self.action_result = 0

    def create_application(self, _pid):
        self.create_calls += 1
        if self.create_calls == 2 and self.before_revalidation is not None:
            self.before_revalidation()
        return self.application_element

    def read(self, element, attribute):
        value = self.attributes.get((element, attribute))
        if isinstance(value, BaseException):
            raise value
        return value

    def is_settable(self, element, attribute):
        return element == self.focused and attribute == "AXValue"

    def perform_action(self, element, action):
        self.actions.append((element, action))
        return self.action_result


class PreparedCodexSubmissionTests(unittest.TestCase):
    def adapter(
        self,
        fixture,
        *,
        workspace=None,
        trusted=lambda: True,
        created_events=None,
        event_flags=None,
        posted_events=None,
        post_event_to_pid=None,
        max_nodes=64,
        max_depth=4,
    ):
        workspace = workspace or SequencedWorkspace(FakeApplication())
        created_events = created_events if created_events is not None else []
        event_flags = event_flags if event_flags is not None else []
        posted_events = posted_events if posted_events is not None else []

        def create_event(_source, key_code, down):
            event = Node("key-down" if down else "key-up")
            created_events.append((event, key_code, down))
            return event

        if post_event_to_pid is None:
            post_event_to_pid = lambda pid, event: posted_events.append(
                (pid, event)
            )
        return MacOSCodexAccessibility(
            trusted=trusted,
            workspace=lambda: workspace,
            create_application=fixture.create_application,
            read_attribute=fixture.read,
            is_attribute_settable=fixture.is_settable,
            perform_action=fixture.perform_action,
            create_key_event=create_event,
            set_event_flags=lambda event, flags: event_flags.append(
                (event, flags)
            ),
            post_event_to_pid=post_event_to_pid,
            max_nodes=max_nodes,
            max_depth=max_depth,
        )

    def test_app_asar_composer_allowlist_covers_every_current_mode(self):
        expected_ids = {
            "chatgptConversations.composer.placeholder",
            "homePage.composer.placeholder.askAnything.v2",
            "composer.placeholder.cloudFollowUp.directFollowUp",
            "composer.placeholder.cloudFollowUp.local",
            "composer.placeholder.cloudFollowUp.workingTree",
            "composer.placeholder.goal",
            "composer.placeholder.localFollowUp.cloud",
            "composer.placeholder.localFollowUp.locally",
            "composer.placeholder.localFollowUp.locallyWithAgents",
            "composer.placeholder.newTask.cloud",
            "composer.placeholder.newTask.doAnything",
            "composer.placeholder.newTask.locally.chatgptDesktop",
            "composer.placeholder.newTask.locally.v2",
            "composer.placeholder.plan",
            "composer.placeholder.workWithChatGPT",
            "composer.placeholder.working",
        }
        self.assertEqual(set(COMPOSER_DESCRIPTION_SOURCES), expected_ids)
        self.assertEqual(
            COMPOSER_DESCRIPTIONS,
            frozenset(
                value
                for pair in COMPOSER_DESCRIPTION_SOURCES.values()
                for value in pair
            ),
        )

    def test_chromium_node_id_attribute_uses_real_ax_literal(self):
        self.assertEqual(AX_CHROME_NODE_ID_ATTRIBUTE, "ChromeAXNodeId")
        self.assertNotEqual(AX_CHROME_NODE_ID_ATTRIBUTE, "AXChromeAXNodeId")

    def test_live_codex_focused_node_exposes_real_chromium_attribute(self):
        import ApplicationServices
        from AppKit import NSWorkspace

        if not ApplicationServices.AXIsProcessTrustedWithOptions(None):
            self.skipTest("Accessibility permission is unavailable")
        application = NSWorkspace.sharedWorkspace().frontmostApplication()
        if (
            application is None
            or application.bundleIdentifier() != CODEX_BUNDLE_IDENTIFIER
        ):
            self.skipTest("Codex is not the foreground application")
        ax_application = ApplicationServices.AXUIElementCreateApplication(
            application.processIdentifier()
        )
        error, focused = ApplicationServices.AXUIElementCopyAttributeValue(
            ax_application,
            ApplicationServices.kAXFocusedUIElementAttribute,
            None,
        )
        if error != ApplicationServices.kAXErrorSuccess or focused is None:
            self.skipTest("Codex has no focused Accessibility element")
        error, attribute_names = (
            ApplicationServices.AXUIElementCopyAttributeNames(focused, None)
        )
        if error != ApplicationServices.kAXErrorSuccess:
            self.skipTest("Could not read focused Accessibility attributes")

        self.assertIn("ChromeAXNodeId", attribute_names)
        self.assertNotIn("AXChromeAXNodeId", attribute_names)

        def read(attribute):
            error, value = ApplicationServices.AXUIElementCopyAttributeValue(
                focused, attribute, None
            )
            return value if error == ApplicationServices.kAXErrorSuccess else None

        role = read(ApplicationServices.kAXRoleAttribute)
        description = read(ApplicationServices.kAXDescriptionAttribute)
        classes = read("AXDOMClassList")
        try:
            class_values = tuple(classes)
        except TypeError:
            class_values = ()
        if not (
            role == ApplicationServices.kAXTextAreaRole
            and description in COMPOSER_DESCRIPTIONS
            and "ProseMirror" in class_values
        ):
            self.skipTest("focused Codex element is not the task composer")

        adapter = MacOSCodexAccessibility()
        current = adapter.snapshot()
        self.assertTrue(current.editable)
        self.assertIn(
            current.semantic,
            (SubmitSemantic.SEND, SubmitSemantic.QUEUE, SubmitSemantic.STEER),
        )
        prepared = adapter.prepare()
        if current.has_text:
            self.assertIsNotNone(prepared)
        else:
            self.assertIsNone(prepared)

    def test_real_two_level_composer_recognizes_en_zh_and_follow_up(self):
        cases = (
            ("Do anything", "Send", SubmitSemantic.SEND),
            ("随心输入", "发送", SubmitSemantic.SEND),
            ("Ask for follow-up changes", "Queue", SubmitSemantic.QUEUE),
            ("提出后续变更要求", "加入队列", SubmitSemantic.QUEUE),
            ("Do anything", "Steer", SubmitSemantic.STEER),
            ("随心输入", "调整方向", SubmitSemantic.STEER),
        )
        for description, label, semantic in cases:
            with self.subTest(description=description, label=label):
                fixture = RealComposerFixture(
                    description=description, label=label
                )
                current = self.adapter(fixture).snapshot()
                self.assertEqual(current, snapshot(semantic))

    def test_bridged_nsarray_classes_and_children_are_normalized(self):
        fixture = RealComposerFixture()
        for element in (
            fixture.focused,
            fixture.rich_text_input,
            fixture.composer_body,
            fixture.composer_root,
        ):
            key = (element, "AXDOMClassList")
            fixture.attributes[key] = NSArrayLike(fixture.attributes[key])
        for element in (
            fixture.focused,
            fixture.rich_text_input,
            fixture.composer_body,
            fixture.composer_root,
        ):
            key = (element, "AXChildren")
            fixture.attributes[key] = NSArrayLike(fixture.attributes[key])

        current = self.adapter(fixture).snapshot()

        self.assertEqual(current, snapshot(SubmitSemantic.SEND))

    def test_exceptional_bridged_collection_fails_closed(self):
        for attribute in ("AXDOMClassList", "AXChildren"):
            with self.subTest(attribute=attribute):
                fixture = RealComposerFixture()
                target = (
                    fixture.focused
                    if attribute == "AXDOMClassList"
                    else fixture.composer_body
                )
                fixture.attributes[(target, attribute)] = ExplodingNSArrayLike()

                current = self.adapter(fixture).snapshot()

                self.assertFalse(current.editable)
                self.assertEqual(current.semantic, SubmitSemantic.UNKNOWN)

    def test_optional_composer_root_can_be_omitted(self):
        fixture = RealComposerFixture()
        fixture.attributes[(fixture.composer_body, "AXParent")] = None
        self.assertEqual(
            self.adapter(fixture).snapshot().semantic,
            SubmitSemantic.SEND,
        )

    def test_feedback_textarea_with_local_send_button_is_rejected(self):
        fixture = RealComposerFixture(description="Share details")
        current = self.adapter(fixture).snapshot()
        self.assertFalse(current.editable)
        self.assertEqual(current.semantic, SubmitSemantic.UNKNOWN)

    def test_missing_disabled_or_wrong_role_focus_is_rejected(self):
        cases = ("missing", "disabled", "wrong-role")
        for case in cases:
            with self.subTest(case=case):
                fixture = RealComposerFixture()
                if case == "missing":
                    fixture.attributes[
                        (fixture.application_element, "AXFocusedUIElement")
                    ] = None
                elif case == "disabled":
                    fixture.attributes[(fixture.focused, "AXEnabled")] = False
                else:
                    fixture.attributes[(fixture.focused, "AXRole")] = (
                        "AXStaticText"
                    )

                current = self.adapter(fixture).snapshot()

                self.assertFalse(current.editable)
                self.assertEqual(current.semantic, SubmitSemantic.UNKNOWN)

    def test_non_codex_permission_and_ax_errors_fail_closed(self):
        fixture = RealComposerFixture()
        non_codex = self.adapter(
            fixture,
            workspace=SequencedWorkspace(
                FakeApplication("com.apple.TextEdit")
            ),
        ).snapshot()
        self.assertFalse(non_codex.codex_foreground)
        self.assertEqual(fixture.create_calls, 0)

        no_permission = self.adapter(fixture, trusted=lambda: False).snapshot()
        self.assertTrue(no_permission.codex_foreground)
        self.assertFalse(no_permission.editable)

        fixture.attributes[(fixture.focused, "AXRole")] = RuntimeError(
            "AX read failed"
        )
        self.assertFalse(self.adapter(fixture).snapshot().editable)

    def test_text_must_be_settable_and_have_stable_focused_token(self):
        fixture = RealComposerFixture()
        adapter = self.adapter(fixture)
        adapter._is_attribute_settable = lambda _element, _attribute: False
        self.assertFalse(adapter.snapshot().editable)

        fixture = RealComposerFixture()
        adapter = self.adapter(fixture)

        def fail_settable(_element, _attribute):
            raise RuntimeError("AX settable check failed")

        adapter._is_attribute_settable = fail_settable
        self.assertFalse(adapter.snapshot().editable)

        fixture = RealComposerFixture()
        fixture.attributes.pop((fixture.focused, "ChromeAXNodeId"))
        self.assertIsNone(self.adapter(fixture).prepare())

    def test_placeholder_subtree_makes_real_ax_value_empty(self):
        fixture = RealComposerFixture(
            description="随心输入", value="\n随心输入"
        )
        placeholder = Node("placeholder")
        fixture.attributes[(placeholder, "AXDOMClassList")] = (
            "_placeholder_abcd_1",
        )
        fixture.attributes[(fixture.focused, "AXChildren")] = (placeholder,)

        current = self.adapter(fixture).snapshot()
        self.assertTrue(current.editable)
        self.assertFalse(current.has_text)
        self.assertIsNone(self.adapter(fixture).prepare())

    def test_button_must_be_unique_enabled_and_have_stable_token(self):
        fixture = RealComposerFixture()
        fixture.attributes[(fixture.button, "AXEnabled")] = False
        self.assertEqual(
            self.adapter(fixture).snapshot().semantic,
            SubmitSemantic.UNKNOWN,
        )

        fixture = RealComposerFixture()
        fixture.attributes.pop((fixture.button, "ChromeAXNodeId"))
        self.assertEqual(
            self.adapter(fixture).snapshot().semantic,
            SubmitSemantic.UNKNOWN,
        )

        fixture = RealComposerFixture()
        second = Node("second-button")
        fixture.attributes[(second, "AXRole")] = "AXButton"
        fixture.attributes[(second, "AXEnabled")] = True
        fixture.attributes[(second, "AXTitle")] = ""
        fixture.attributes[(second, "AXDescription")] = "Send"
        fixture.attributes[(second, "ChromeAXNodeId")] = 303
        fixture.attributes[(fixture.composer_body, "AXChildren")] += (second,)
        self.assertEqual(
            self.adapter(fixture).snapshot().semantic,
            SubmitSemantic.UNKNOWN,
        )

    def test_same_button_with_conflicting_semantic_labels_is_rejected(self):
        fixture = RealComposerFixture(label="Queue")
        fixture.attributes[(fixture.button, "AXTitle")] = "Send"

        current = self.adapter(fixture).snapshot()

        self.assertEqual(current.semantic, SubmitSemantic.UNKNOWN)

    def test_composer_button_search_respects_node_and_depth_bounds(self):
        fixture = RealComposerFixture()
        extras = tuple(Node("extra-{}".format(index)) for index in range(5))
        for extra in extras:
            fixture.attributes[(extra, "AXRole")] = "AXGroup"
        fixture.attributes[(fixture.composer_body, "AXChildren")] = (
            fixture.rich_text_input,
            *extras,
            fixture.button,
        )
        self.assertEqual(
            self.adapter(fixture, max_nodes=3).snapshot().semantic,
            SubmitSemantic.UNKNOWN,
        )

        fixture = RealComposerFixture()
        wrapper = Node("button-wrapper")
        fixture.attributes[(wrapper, "AXRole")] = "AXGroup"
        fixture.attributes[(wrapper, "AXChildren")] = (fixture.button,)
        fixture.attributes[(fixture.composer_body, "AXChildren")] = (
            fixture.rich_text_input,
            wrapper,
        )
        self.assertEqual(
            self.adapter(fixture, max_depth=1).snapshot().semantic,
            SubmitSemantic.UNKNOWN,
        )

    def test_normal_and_force_send_or_steer_revalidate_then_ax_press(self):
        cases = (
            (InputAction.SUBMIT, "Send"),
            (InputAction.SUBMIT, "Queue"),
            (InputAction.FORCE_STEER, "Send"),
            (InputAction.FORCE_STEER, "Steer"),
        )
        for action, label in cases:
            with self.subTest(action=action, label=label):
                fixture = RealComposerFixture(label=label)
                handled = CodexSubmitController(self.adapter(fixture)).handle(action)
                self.assertTrue(handled)
                self.assertEqual(
                    fixture.actions,
                    [(fixture.button, "AXPress")],
                )
                self.assertEqual(fixture.create_calls, 2)

    def test_target_change_between_prepare_and_dispatch_fails_closed(self):
        mutation_cases = (
            ("focus", lambda fixture: fixture.attributes.__setitem__(
                (fixture.application_element, "AXFocusedUIElement"), Node("other")
            )),
            ("focus-token", lambda fixture: fixture.attributes.__setitem__(
                (fixture.focused, "ChromeAXNodeId"), 999
            )),
            ("value-clear", lambda fixture: fixture.attributes.__setitem__(
                (fixture.focused, "AXValue"), ""
            )),
            ("value-change", lambda fixture: fixture.attributes.__setitem__(
                (fixture.focused, "AXValue"), "changed"
            )),
            ("semantic", lambda fixture: fixture.attributes.__setitem__(
                (fixture.button, "AXDescription"), "Queue"
            )),
            ("button-disabled", lambda fixture: fixture.attributes.__setitem__(
                (fixture.button, "AXEnabled"), False
            )),
            ("button-token", lambda fixture: fixture.attributes.__setitem__(
                (fixture.button, "ChromeAXNodeId"), 303
            )),
        )
        for name, mutate in mutation_cases:
            with self.subTest(case=name):
                fixture = RealComposerFixture()
                fixture.before_revalidation = lambda f=fixture, m=mutate: m(f)
                handled = CodexSubmitController(self.adapter(fixture)).handle(
                    InputAction.SUBMIT
                )
                self.assertFalse(handled)
                self.assertEqual(fixture.actions, [])

        for name, applications in (
            (
                "app-switch",
                (FakeApplication(), FakeApplication("com.apple.TextEdit")),
            ),
            (
                "pid-change",
                (FakeApplication(pid=42), FakeApplication(pid=43)),
            ),
        ):
            with self.subTest(case=name):
                fixture = RealComposerFixture()
                handled = CodexSubmitController(
                    self.adapter(fixture, workspace=SequencedWorkspace(*applications))
                ).handle(InputAction.SUBMIT)
                self.assertFalse(handled)
                self.assertEqual(fixture.actions, [])

    def test_ax_press_error_fails_closed_without_keyboard_fallback(self):
        fixture = RealComposerFixture()
        fixture.action_result = -1
        posted = []
        handled = CodexSubmitController(
            self.adapter(fixture, posted_events=posted)
        ).handle(InputAction.SUBMIT)
        self.assertFalse(handled)
        self.assertEqual(posted, [])

    def test_force_queue_posts_complete_modified_return_to_captured_pid(self):
        fixture = RealComposerFixture(label="Queue")
        created = []
        flags = []
        posted = []
        adapter = self.adapter(
            fixture,
            created_events=created,
            event_flags=flags,
            posted_events=posted,
        )

        handled = CodexSubmitController(adapter).handle(InputAction.FORCE_STEER)

        self.assertTrue(handled)
        self.assertEqual(fixture.actions, [])
        self.assertEqual(
            [(key_code, down) for _, key_code, down in created],
            [(RETURN_KEY_CODE, True), (RETURN_KEY_CODE, False)],
        )
        self.assertEqual(
            [value for _, value in flags],
            [ALTERNATE_MODIFIER_FLAGS, ALTERNATE_MODIFIER_FLAGS],
        )
        self.assertEqual(
            posted,
            [(42, created[0][0]), (42, created[1][0])],
        )

    def test_missing_targeted_key_event_fails_before_posting(self):
        fixture = RealComposerFixture(label="Queue")
        posted = []
        adapter = self.adapter(fixture, posted_events=posted)
        adapter._create_key_event = lambda _source, _key_code, _down: None

        handled = CodexSubmitController(adapter).handle(
            InputAction.FORCE_STEER
        )

        self.assertFalse(handled)
        self.assertEqual(posted, [])

    def test_targeted_key_up_is_attempted_after_key_down_error(self):
        fixture = RealComposerFixture(label="Queue")
        attempts = []

        def post_event(pid, event):
            attempts.append((pid, event.token))
            if event.token == "key-down":
                raise RuntimeError("post failed")

        handled = CodexSubmitController(
            self.adapter(fixture, post_event_to_pid=post_event)
        ).handle(InputAction.FORCE_STEER)

        self.assertFalse(handled)
        self.assertEqual(attempts, [(42, "key-down"), (42, "key-up")])

    def test_equal_ax_wrappers_break_cycles_without_hiding_button(self):
        fixture = RealComposerFixture()
        equal_body_wrapper = Node("composer-body")
        fixture.attributes[(fixture.composer_body, "AXChildren")] = (
            equal_body_wrapper,
            fixture.rich_text_input,
            fixture.button,
        )
        current = self.adapter(fixture).snapshot()
        self.assertEqual(current.semantic, SubmitSemantic.SEND)


if __name__ == "__main__":
    unittest.main()
