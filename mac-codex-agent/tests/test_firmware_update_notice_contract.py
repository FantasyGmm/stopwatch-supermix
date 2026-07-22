import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class FirmwareUpdateNoticeContractTests(unittest.TestCase):
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
