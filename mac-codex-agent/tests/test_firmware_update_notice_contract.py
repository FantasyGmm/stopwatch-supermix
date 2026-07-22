import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class FirmwareUpdateNoticeContractTests(unittest.TestCase):
    def test_app_event_switch_is_exhaustive_and_update_notice_is_a_no_op(self):
        mailbox_h = (ROOT / "main/runtime/assistant/assistant_event_mailbox.h").read_text(encoding="utf-8")
        app = (ROOT / "main/apps/app_assistant/app_assistant.cpp").read_text(encoding="utf-8")

        enum_body = re.search(r"enum class AssistantEventKind[^\{]*\{(?P<body>.*?)\};", mailbox_h, re.S)
        self.assertIsNotNone(enum_body)
        event_kinds = {
            entry.strip().split("=")[0].strip()
            for entry in enum_body.group("body").split(",")
            if entry.strip()
        }

        function_start = app.index("void AppAssistant::applyRuntimeEvent")
        function_body = app[function_start:]
        handled_kinds = set(re.findall(r"case AssistantEventKind::(\w+):", function_body))

        self.assertEqual(handled_kinds, event_kinds)
        self.assertNotIn("default:", function_body)
        self.assertRegex(
            function_body,
            r"case AssistantEventKind::UpdateAvailable:\s*break;",
        )

    def test_notice_uses_product_nvs_and_a_persistent_mailbox_slot(self):
        store = (ROOT / "main/runtime/assistant/assistant_update_notice_store.cpp").read_text(encoding="utf-8")
        mailbox_h = (ROOT / "main/runtime/assistant/assistant_event_mailbox.h").read_text(encoding="utf-8")
        mailbox_cpp = (ROOT / "main/runtime/assistant/assistant_event_mailbox.cpp").read_text(encoding="utf-8")
        runtime = (ROOT / "main/runtime/assistant/assistant_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn('"assistant_ui"', store)
        self.assertIn('"update_ver"', store)
        self.assertIn("nvs_commit", store)
        self.assertIn("UpdateAvailable", mailbox_h)
        self.assertIn("AssistantEventKind::UpdateAvailable", mailbox_cpp)
        self.assertIn("AssistantUpdateNoticeStore", runtime)
        self.assertIn("GetAssistantUpdateNoticeBridge", runtime)

    def test_xiaozhi_overlay_reports_version_without_install_inputs(self):
        overlay = (ROOT / "main/cmake/brookesia_xiaozhi_mcp_ownership_overlay.cmake").read_text(encoding="utf-8")
        self.assertIn("stopwatch_duplicate_firmware_version", overlay)
        self.assertIn("stopwatch_capture_firmware_update_session_token", overlay)
        self.assertIn("stopwatch_report_remote_firmware_version", overlay)
        self.assertIn(
            "stopwatch_report_remote_firmware_version(update_notice_session_token, info.firmware_version)",
            overlay,
        )
        self.assertIn("info.current_version", overlay)
        self.assertIn("info.firmware_version", overlay)
        self.assertNotIn("info.firmware_url", overlay)
        self.assertNotIn("force", overlay)
        self.assertNotIn("install", overlay)
        self.assertNotIn("esp_https_ota", overlay)
        self.assertNotIn("esp_ota_", overlay)

    def test_xiaozhi_overlay_frees_failed_info_before_return_guard(self):
        overlay = (ROOT / "main/cmake/brookesia_xiaozhi_mcp_ownership_overlay.cmake").read_text(encoding="utf-8")
        request_start = overlay.index("set(versioned_info_request")
        request_end = overlay.index("]=])", request_start)
        request = overlay[request_start:request_end]

        self.assertIn("if (ret != ESP_OK)", request)
        get_info = request.index("esp_xiaozhi_chat_get_info(&info)")
        failure_cleanup = request.index("esp_xiaozhi_chat_free_info(&info)", get_info)
        failure_return_guard = request.index("BROOKESIA_CHECK_ESP_ERR_RETURN", failure_cleanup)
        success_guard = request.index("lib_utils::FunctionGuard free_info_guard", failure_return_guard)
        success_cleanup = request.index("esp_xiaozhi_chat_free_info(&info)", success_guard)

        self.assertLess(get_info, failure_cleanup)
        self.assertLess(failure_cleanup, failure_return_guard)
        self.assertLess(failure_return_guard, success_guard)
        self.assertLess(success_guard, success_cleanup)
        self.assertEqual(request.count("esp_xiaozhi_chat_free_info(&info)"), 2)


if __name__ == "__main__":
    unittest.main()
