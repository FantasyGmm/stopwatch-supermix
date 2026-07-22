import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class AssistantSignalCoreUiContractTests(unittest.TestCase):
    def callback_guard_body(self, runtime, event_name):
        callback = re.search(
            rf"ManagerHelper::EventId::{event_name},\s*"
            r"\[this, session_id\]\(const std::string& source, bool \w+\) \{"
            r"(?P<body>.*?)\n        \}\);",
            runtime,
            re.S,
        )
        self.assertIsNotNone(callback)
        guard = re.search(
            rf"if \(callbackIsCurrentXiaozhi\(\s*session_id,\s*source,\s*"
            rf"manager_event_name\(ManagerHelper::EventId::{event_name}\)\)\) \{{"
            r"(?P<body>.*?)\n            \}",
            callback.group("body"),
            re.S,
        )
        self.assertIsNotNone(guard)
        return guard.group("body")

    def test_runtime_forwards_real_listening_and_speaking_status(self):
        mailbox = (ROOT / "main/runtime/assistant/assistant_event_mailbox.h").read_text()
        runtime = (ROOT / "main/runtime/assistant/assistant_runtime.cpp").read_text()

        self.assertIn("ListeningStatus", mailbox)
        self.assertIn("SpeakingStatus", mailbox)
        listening_guard = self.callback_guard_body(runtime, "ListeningStatusChanged")
        speaking_guard = self.callback_guard_body(runtime, "SpeakingStatusChanged")
        self.assertRegex(
            listening_guard,
            r"mailbox\.publish\(\{session_id,\s*"
            r"AssistantEventKind::ListeningStatus,\s*\{\},\s*listening\}\)",
        )
        self.assertRegex(
            speaking_guard,
            r"mailbox\.publish\(\{session_id,\s*"
            r"AssistantEventKind::SpeakingStatus,\s*\{\},\s*speaking\}\)",
        )

    def test_close_suppression_includes_listening_and_speaking_status(self):
        app = (ROOT / "main/apps/app_assistant/app_assistant.cpp").read_text()
        conversation_events = re.search(
            r"bool isConversationEvent\(AssistantEventKind kind\)\s*\{(?P<body>.*?)\n\}",
            app,
            re.S,
        )

        self.assertIsNotNone(conversation_events)
        self.assertIn("AssistantEventKind::ListeningStatus", conversation_events.group("body"))
        self.assertIn("AssistantEventKind::SpeakingStatus", conversation_events.group("body"))


if __name__ == "__main__":
    unittest.main()
