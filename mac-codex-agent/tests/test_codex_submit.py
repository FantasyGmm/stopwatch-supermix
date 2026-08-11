import pathlib
import sys
import unittest


AGENT_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AGENT_ROOT))

from codex_submit import (  # noqa: E402
    ALTERNATE_MODIFIER_FLAGS,
    CODEX_BUNDLE_IDENTIFIER,
    RETURN_KEY_CODE,
    CodexSubmitController,
    ComposerSnapshot,
    MacOSCodexAccessibility,
    SubmitSemantic,
)
from input_action_protocol import InputAction  # noqa: E402


class FakeAccessibility:
    def __init__(self, snapshot):
        self.current_snapshot = snapshot
        self.normal_calls = 0
        self.alternate_calls = 0

    def snapshot(self):
        if isinstance(self.current_snapshot, BaseException):
            raise self.current_snapshot
        return self.current_snapshot

    def normal_submit(self):
        self.normal_calls += 1

    def alternate_submit(self):
        self.alternate_calls += 1


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


class CodexSubmitControllerTests(unittest.TestCase):
    def dispatch(self, action, current_snapshot):
        accessibility = FakeAccessibility(current_snapshot)
        handled = CodexSubmitController(accessibility).handle(action)
        return handled, accessibility

    def test_submit_uses_normal_action_for_every_recognized_semantic(self):
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

    def test_force_steer_uses_normal_action_for_send_or_steer(self):
        for semantic in (SubmitSemantic.SEND, SubmitSemantic.STEER):
            with self.subTest(semantic=semantic):
                handled, accessibility = self.dispatch(
                    InputAction.FORCE_STEER, snapshot(semantic)
                )

                self.assertTrue(handled)
                self.assertEqual(accessibility.normal_calls, 1)
                self.assertEqual(accessibility.alternate_calls, 0)

    def test_force_steer_uses_alternate_action_when_normal_action_queues(self):
        handled, accessibility = self.dispatch(
            InputAction.FORCE_STEER, snapshot(SubmitSemantic.QUEUE)
        )

        self.assertTrue(handled)
        self.assertEqual(accessibility.normal_calls, 0)
        self.assertEqual(accessibility.alternate_calls, 1)

    def test_every_unsafe_snapshot_fails_closed(self):
        unsafe_snapshots = (
            snapshot(codex_foreground=False),
            snapshot(editable=False),
            snapshot(has_text=False),
            snapshot(SubmitSemantic.UNKNOWN),
        )
        for current_snapshot in unsafe_snapshots:
            for action in (InputAction.SUBMIT, InputAction.FORCE_STEER):
                with self.subTest(snapshot=current_snapshot, action=action):
                    handled, accessibility = self.dispatch(action, current_snapshot)

                    self.assertFalse(handled)
                    self.assertEqual(accessibility.normal_calls, 0)
                    self.assertEqual(accessibility.alternate_calls, 0)

    def test_unknown_action_fails_closed(self):
        handled, accessibility = self.dispatch(object(), snapshot())

        self.assertFalse(handled)
        self.assertEqual(accessibility.normal_calls, 0)
        self.assertEqual(accessibility.alternate_calls, 0)

    def test_accessibility_errors_fail_closed(self):
        handled, accessibility = self.dispatch(
            InputAction.SUBMIT, RuntimeError("AX unavailable")
        )

        self.assertFalse(handled)
        self.assertEqual(accessibility.normal_calls, 0)

    def test_malformed_accessibility_snapshot_fails_closed(self):
        malformed_snapshots = (
            object(),
            ComposerSnapshot(True, True, True, object()),
        )
        for current_snapshot in malformed_snapshots:
            with self.subTest(snapshot=current_snapshot):
                handled, accessibility = self.dispatch(
                    InputAction.SUBMIT, current_snapshot
                )

                self.assertFalse(handled)
                self.assertEqual(accessibility.normal_calls, 0)

    def test_keyboard_dispatch_errors_fail_closed(self):
        accessibility = FakeAccessibility(snapshot())

        def fail():
            raise RuntimeError("event posting failed")

        accessibility.normal_submit = fail
        handled = CodexSubmitController(accessibility).handle(InputAction.SUBMIT)

        self.assertFalse(handled)


class FakeApplication:
    def __init__(self, bundle_identifier=CODEX_BUNDLE_IDENTIFIER, pid=42):
        self._bundle_identifier = bundle_identifier
        self._pid = pid

    def bundleIdentifier(self):
        return self._bundle_identifier

    def processIdentifier(self):
        return self._pid


class FakeWorkspace:
    def __init__(self, application):
        self.application = application

    def frontmostApplication(self):
        return self.application


class Node:
    pass


class FakeAX:
    def __init__(self, focused, attributes):
        self.application = Node()
        self.focused = focused
        self.attributes = attributes
        self.reads = []

    def create_application(self, pid):
        if pid != 42:
            raise AssertionError("unexpected pid")
        return self.application

    def read(self, element, attribute):
        self.reads.append((element, attribute))
        if element is self.application and attribute == "AXFocusedUIElement":
            return self.focused
        value = self.attributes.get((element, attribute))
        if isinstance(value, BaseException):
            raise value
        return value


def composer_graph(*, value="draft", label="Send", label_attribute="AXTitle"):
    composer = Node()
    container = Node()
    button = Node()
    attributes = {
        (composer, "AXRole"): "AXTextArea",
        (composer, "AXEnabled"): True,
        (composer, "AXValue"): value,
        (composer, "AXParent"): container,
        (container, "AXChildren"): (composer, button),
        (button, "AXRole"): "AXButton",
        (button, label_attribute): label,
    }
    return composer, container, button, attributes


class MacOSCodexAccessibilitySnapshotTests(unittest.TestCase):
    def adapter(
        self,
        fake_ax,
        *,
        bundle_identifier=CODEX_BUNDLE_IDENTIFIER,
        trusted=lambda: True,
        max_nodes=64,
        max_depth=6,
    ):
        return MacOSCodexAccessibility(
            trusted=trusted,
            workspace=lambda: FakeWorkspace(FakeApplication(bundle_identifier)),
            create_application=fake_ax.create_application,
            read_attribute=fake_ax.read,
            max_nodes=max_nodes,
            max_depth=max_depth,
        )

    def test_non_codex_foreground_never_reads_accessibility_tree(self):
        composer, _container, _button, attributes = composer_graph()
        fake_ax = FakeAX(composer, attributes)

        current = self.adapter(
            fake_ax, bundle_identifier="com.apple.TextEdit"
        ).snapshot()

        self.assertEqual(current, snapshot(codex_foreground=False, editable=False,
                                           has_text=False,
                                           semantic=SubmitSemantic.UNKNOWN))
        self.assertEqual(fake_ax.reads, [])

    def test_missing_accessibility_permission_fails_closed(self):
        composer, _container, _button, attributes = composer_graph()
        fake_ax = FakeAX(composer, attributes)

        current = self.adapter(fake_ax, trusted=lambda: False).snapshot()

        self.assertEqual(
            current,
            snapshot(editable=False, has_text=False,
                     semantic=SubmitSemantic.UNKNOWN),
        )
        self.assertEqual(fake_ax.reads, [])

    def test_focused_editable_nonempty_composer_and_english_labels(self):
        cases = (
            ("Send", SubmitSemantic.SEND),
            ("Queue", SubmitSemantic.QUEUE),
            ("Steer", SubmitSemantic.STEER),
        )
        for label, semantic in cases:
            with self.subTest(label=label):
                composer, _container, _button, attributes = composer_graph(
                    label=label
                )

                current = self.adapter(FakeAX(composer, attributes)).snapshot()

                self.assertEqual(current, snapshot(semantic))

    def test_simplified_chinese_labels_and_all_supported_attributes(self):
        cases = (
            ("发送", "AXTitle", SubmitSemantic.SEND),
            ("加入队列", "AXDescription", SubmitSemantic.QUEUE),
            ("调整方向", "AXHelp", SubmitSemantic.STEER),
        )
        for label, attribute, semantic in cases:
            with self.subTest(label=label, attribute=attribute):
                composer, _container, _button, attributes = composer_graph(
                    label=label, label_attribute=attribute
                )

                current = self.adapter(FakeAX(composer, attributes)).snapshot()

                self.assertEqual(current, snapshot(semantic))

    def test_text_must_be_nonempty_after_trimming(self):
        composer, _container, _button, attributes = composer_graph(value=" \n\t ")

        current = self.adapter(FakeAX(composer, attributes)).snapshot()

        self.assertTrue(current.editable)
        self.assertFalse(current.has_text)
        self.assertEqual(current.semantic, SubmitSemantic.SEND)

    def test_missing_or_non_editable_focused_element_fails_closed(self):
        cases = []
        composer, _container, _button, attributes = composer_graph()
        attributes[(composer, "AXEnabled")] = False
        cases.append((composer, attributes))
        composer, _container, _button, attributes = composer_graph()
        attributes[(composer, "AXRole")] = "AXStaticText"
        cases.append((composer, attributes))
        cases.append((None, {}))

        for focused, attributes in cases:
            with self.subTest(focused=focused):
                current = self.adapter(FakeAX(focused, attributes)).snapshot()

                self.assertFalse(current.editable)
                self.assertFalse(current.has_text)
                self.assertEqual(current.semantic, SubmitSemantic.UNKNOWN)

    def test_conflicting_or_unrecognized_button_labels_are_unknown(self):
        for conflicting in (False, True):
            with self.subTest(conflicting=conflicting):
                composer, container, button, attributes = composer_graph(
                    label="Submit message"
                )
                if conflicting:
                    second_button = Node()
                    attributes[(second_button, "AXRole")] = "AXButton"
                    attributes[(second_button, "AXTitle")] = "Queue"
                    attributes[(button, "AXTitle")] = "Send"
                    attributes[(container, "AXChildren")] = (
                        composer,
                        button,
                        second_button,
                    )

                current = self.adapter(FakeAX(composer, attributes)).snapshot()

                self.assertEqual(current.semantic, SubmitSemantic.UNKNOWN)

    def test_traversal_is_cycle_safe_and_respects_node_limit(self):
        composer, container, _button, attributes = composer_graph(label="unknown")
        extra_nodes = [Node() for _ in range(10)]
        semantic_button = Node()
        attributes[(container, "AXChildren")] = tuple(
            [composer, container, *extra_nodes, semantic_button]
        )
        for node in extra_nodes:
            attributes[(node, "AXRole")] = "AXGroup"
        attributes[(semantic_button, "AXRole")] = "AXButton"
        attributes[(semantic_button, "AXTitle")] = "Send"
        fake_ax = FakeAX(composer, attributes)

        current = self.adapter(fake_ax, max_nodes=4, max_depth=3).snapshot()

        self.assertEqual(current.semantic, SubmitSemantic.UNKNOWN)
        visited = {
            element
            for element, attribute in fake_ax.reads
            if attribute == "AXRole"
        }
        self.assertLessEqual(len(visited), 4)

    def test_traversal_respects_depth_limit(self):
        composer, _container, _button, attributes = composer_graph(label="Send")

        current = self.adapter(
            FakeAX(composer, attributes), max_depth=1
        ).snapshot()

        self.assertEqual(current.semantic, SubmitSemantic.UNKNOWN)

    def test_ax_read_exception_fails_closed(self):
        composer, _container, _button, attributes = composer_graph()
        attributes[(composer, "AXRole")] = RuntimeError("AX read failed")

        current = self.adapter(FakeAX(composer, attributes)).snapshot()

        self.assertEqual(
            current,
            snapshot(editable=False, has_text=False,
                     semantic=SubmitSemantic.UNKNOWN),
        )


class MacOSCodexAccessibilityKeyboardTests(unittest.TestCase):
    def adapter_and_events(self):
        created = []
        flags = []
        posted = []

        def create_event(source, key_code, down):
            event = object()
            created.append((event, source, key_code, down))
            return event

        adapter = MacOSCodexAccessibility(
            create_key_event=create_event,
            set_event_flags=lambda event, value: flags.append((event, value)),
            post_event=lambda tap, event: posted.append((tap, event)),
        )
        return adapter, created, flags, posted

    def test_normal_submit_posts_complete_return_key_tap(self):
        adapter, created, flags, posted = self.adapter_and_events()

        adapter.normal_submit()

        self.assertEqual(
            [(source, key_code, down) for _, source, key_code, down in created],
            [(None, RETURN_KEY_CODE, True), (None, RETURN_KEY_CODE, False)],
        )
        self.assertEqual(flags, [])
        self.assertEqual([event for _, event in posted], [created[0][0], created[1][0]])

    def test_alternate_submit_posts_complete_modified_return_key_tap(self):
        adapter, created, flags, posted = self.adapter_and_events()

        adapter.alternate_submit()

        self.assertEqual(
            [(source, key_code, down) for _, source, key_code, down in created],
            [(None, RETURN_KEY_CODE, True), (None, RETURN_KEY_CODE, False)],
        )
        self.assertEqual(len(flags), 2)
        self.assertEqual(flags[0][0], created[0][0])
        self.assertEqual(flags[1][0], created[1][0])
        self.assertEqual(
            [value for _, value in flags],
            [ALTERNATE_MODIFIER_FLAGS, ALTERNATE_MODIFIER_FLAGS],
        )
        self.assertEqual([event for _, event in posted], [created[0][0], created[1][0]])

    def test_key_up_is_attempted_if_key_down_post_raises(self):
        created = []
        post_attempts = []

        def create_event(_source, _key_code, down):
            event = "down" if down else "up"
            created.append(event)
            return event

        def post_event(_tap, event):
            post_attempts.append(event)
            if event == "down":
                raise RuntimeError("post failed")

        adapter = MacOSCodexAccessibility(
            create_key_event=create_event,
            post_event=post_event,
        )

        with self.assertRaises(RuntimeError):
            adapter.normal_submit()

        self.assertEqual(created, ["down", "up"])
        self.assertEqual(post_attempts, ["down", "up"])

    def test_missing_keyboard_event_raises_before_posting(self):
        posted = []
        adapter = MacOSCodexAccessibility(
            create_key_event=lambda _source, _key_code, _down: None,
            post_event=lambda tap, event: posted.append((tap, event)),
        )

        with self.assertRaises(RuntimeError):
            adapter.normal_submit()

        self.assertEqual(posted, [])


if __name__ == "__main__":
    unittest.main()
