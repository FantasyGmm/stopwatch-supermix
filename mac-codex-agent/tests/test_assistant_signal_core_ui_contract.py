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

    def test_view_uses_fixed_signal_core_layout_without_new_assets(self):
        header = (ROOT / "main/apps/app_assistant/view/assistant_view.h").read_text()
        source = (ROOT / "main/apps/app_assistant/view/assistant_view.cpp").read_text()
        combined = header + source

        self.assertIn("std::array<lv_obj_t*, 9>", header)
        self.assertIn("void update(uint32_t now_ms)", header)
        self.assertIn("setBattery", header)
        self.assertIn("setConnected", header)
        self.assertIn("font_puhui_14_1", source)
        self.assertNotIn("实时字幕", combined)
        self.assertNotRegex(combined, r"lv_timer_create|xTaskCreate|TaskHandle_t|FFT|gif")
        self.assertNotRegex(combined, r"LV_ALIGN_BOTTOM_MID,\s*0,\s*-[0-3][0-9]\b")

    def test_waiting_state_is_visually_distinct_and_silent(self):
        source = (ROOT / "main/apps/app_assistant/view/assistant_view.cpp").read_text()

        self.assertIn("AssistantVisualState::WaitingWake", source)
        self.assertIn("等待唤醒", source)
        self.assertIn("你好小智", source)
        self.assertIn("inactive_wave", source)


if __name__ == "__main__":
    unittest.main()
