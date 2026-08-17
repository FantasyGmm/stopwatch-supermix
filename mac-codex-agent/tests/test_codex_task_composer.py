import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from codex_task_composer import CodexTaskComposerClient, TaskComposerError


class Node:
    def __init__(self, token):
        self.token = token

    def __hash__(self):
        return hash(self.token)

    def __eq__(self, other):
        return isinstance(other, Node) and self.token == other.token


class FakeApplication:
    def bundleIdentifier(self):
        return "com.openai.codex"

    def processIdentifier(self):
        return 42


class FakeWorkspace:
    def frontmostApplication(self):
        return FakeApplication()


class ComposerFixture:
    def __init__(
        self,
        button_after_write="Send",
        initial_value="",
        has_placeholder=None,
    ):
        if has_placeholder is None:
            has_placeholder = not initial_value
        self.app = Node("app")
        self.root = Node("root")
        self.body = Node("body")
        self.rich = Node("rich")
        self.composer = Node("composer")
        self.placeholder = Node("placeholder")
        self.button = Node("button")
        self.button_after_write = button_after_write
        self.opened = []
        self.actions = []
        self.events = []
        self.posted = []
        self.flags = []
        self.attributes = {
            (self.app, "AXChildren"): (self.root,),
            (self.root, "AXChildren"): (self.body,),
            (self.body, "AXChildren"): (self.rich, self.button),
            (self.body, "AXDOMClassList"): ("_ComposerLayoutBody_live_1",),
            (self.rich, "AXChildren"): (self.composer,),
            (self.rich, "AXParent"): self.body,
            (self.rich, "AXDOMClassList"): ("_RichTextInput_live_1",),
            (self.composer, "AXRole"): "AXTextArea",
            (self.composer, "AXEnabled"): True,
            (self.composer, "AXHidden"): False,
            (self.composer, "AXDescription"): "随心输入",
            (self.composer, "AXDOMClassList"): ("ProseMirror",),
            (self.composer, "ChromeAXNodeId"): 101,
            (self.composer, "AXValue"): initial_value,
            (self.composer, "AXParent"): self.rich,
            (self.composer, "AXChildren"): (
                (self.placeholder,) if has_placeholder else ()
            ),
            (self.placeholder, "AXDOMClassList"): ("placeholder",),
            (self.button, "AXRole"): "AXButton",
            (self.button, "AXEnabled"): True,
            (self.button, "AXHidden"): False,
            (self.button, "AXTitle"): "",
            (self.button, "AXDescription"): "Stop",
            (self.button, "AXHelp"): None,
            (self.button, "ChromeAXNodeId"): 202,
        }

    def open_url(self, url):
        self.opened.append(url)
        self.events.append("open")
        return True

    def create_application(self, pid):
        self.events.append(("application", pid))
        return self.app

    def read(self, element, attribute):
        return self.attributes.get((element, attribute))

    def is_settable(self, element, attribute):
        return element == self.composer and attribute == "AXValue"

    def set_attribute(self, element, attribute, value):
        self.events.append(("write", value))
        self.attributes[(element, attribute)] = value
        self.attributes[(self.composer, "AXChildren")] = ()
        self.attributes[(self.button, "AXDescription")] = self.button_after_write
        return 0

    def perform_action(self, element, action):
        self.events.append("press")
        self.actions.append((element, action))
        return 0

    def create_key_event(self, _source, key_code, down):
        event = Node("down" if down else "up")
        self.events.append(("key", key_code, down))
        return event

    def set_flags(self, event, flags):
        self.flags.append((event, flags))

    def post_to_pid(self, pid, event):
        self.posted.append((pid, event))

    def client(self):
        return CodexTaskComposerClient(
            open_url=self.open_url,
            trusted=lambda: True,
            workspace=lambda: FakeWorkspace(),
            create_application=self.create_application,
            read_attribute=self.read,
            is_attribute_settable=self.is_settable,
            set_attribute=self.set_attribute,
            perform_action=self.perform_action,
            create_key_event=self.create_key_event,
            set_event_flags=self.set_flags,
            post_event_to_pid=self.post_to_pid,
            sleep=lambda _seconds: None,
            timeout=0.1,
        )


class CodexTaskComposerClientTests(unittest.IsolatedAsyncioTestCase):
    async def test_start_turn_opens_exact_thread_writes_then_presses_send(self):
        fixture = ComposerFixture(button_after_write="Send")

        await fixture.client().start_turn("thread-safe", "next instruction")

        self.assertEqual(fixture.opened, ["codex://threads/thread-safe"])
        self.assertIn(("write", "next instruction"), fixture.events)
        self.assertEqual(fixture.actions, [(fixture.button, "AXPress")])
        self.assertLess(fixture.events.index("open"), fixture.events.index(("write", "next instruction")))
        self.assertLess(fixture.events.index(("write", "next instruction")), fixture.events.index("press"))

    async def test_running_turn_uses_targeted_command_return_for_queue(self):
        fixture = ComposerFixture(button_after_write="Queue")

        await fixture.client().steer_turn(
            "thread-running", "turn-current", "guide this task"
        )

        self.assertEqual(fixture.opened, ["codex://threads/thread-running"])
        self.assertEqual(
            [(pid, event.token) for pid, event in fixture.posted],
            [(42, "down"), (42, "up")],
        )
        self.assertEqual(fixture.actions, [])

    async def test_start_turn_steers_when_the_live_task_still_shows_queue(self):
        fixture = ComposerFixture(button_after_write="Queue")

        await fixture.client().start_turn(
            "thread-state-lag", "continue immediately"
        )

        self.assertEqual(
            [(pid, event.token) for pid, event in fixture.posted],
            [(42, "down"), (42, "up")],
        )
        self.assertEqual(fixture.actions, [])

    async def test_existing_draft_fails_closed_without_overwriting_or_sending(self):
        fixture = ComposerFixture(initial_value="existing draft")

        with self.assertRaises(TaskComposerError):
            await fixture.client().start_turn("thread-safe", "new voice text")

        self.assertNotIn(("write", "new voice text"), fixture.events)
        self.assertEqual(fixture.actions, [])
        self.assertEqual(fixture.posted, [])

    async def test_failed_dispatch_clears_a_placeholder_value_not_a_draft(self):
        fixture = ComposerFixture(
            button_after_write="Stop",
            initial_value="随心输入",
            has_placeholder=True,
        )

        with self.assertRaises(TaskComposerError):
            await fixture.client().start_turn("thread-safe", "private voice text")

        self.assertEqual(
            fixture.attributes[(fixture.composer, "AXValue")], ""
        )


if __name__ == "__main__":
    unittest.main()
