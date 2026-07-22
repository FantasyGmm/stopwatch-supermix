import csv
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class FirmwareUpdateNoticeContractTests(unittest.TestCase):
    def test_remote_install_is_structurally_disabled(self):
        rows = []
        with (ROOT / "partitions.csv").open(newline="", encoding="utf-8") as handle:
            for row in csv.reader(handle):
                values = [value.strip() for value in row]
                if values and values[0] and not values[0].startswith("#"):
                    self.assertGreaterEqual(
                        len(values), 3, f"malformed partition CSV row: {values!r}"
                    )
                    rows.append(values)
        apps = [row for row in rows if row[1] == "app"]
        self.assertEqual([(row[0], row[2]) for row in apps], [("app", "factory")])
        self.assertFalse(any(row[0] == "otadata" for row in rows))
        self.assertFalse(any(row[2] in ("ota_0", "ota_1") for row in apps))

    def test_product_sources_have_no_remote_install_path(self):
        forbidden = re.compile(
            r"esp_https_ota|esp_ota_begin|esp_ota_write|"
            r"esp_ota_end|esp_ota_set_boot_partition|esp_restart"
        )
        source_suffixes = {
            ".c",
            ".cc",
            ".cpp",
            ".cxx",
            ".h",
            ".hh",
            ".hpp",
            ".hxx",
            ".cmake",
        }
        offenders = []
        roots = (
            ROOT / "main/runtime/assistant",
            ROOT / "main/apps/app_assistant",
            ROOT / "main/cmake",
        )
        for root in roots:
            for path in root.rglob("*"):
                if path.name == "CMakeLists.txt" or path.suffix in source_suffixes:
                    if forbidden.search(path.read_text(encoding="utf-8")):
                        offenders.append(path.relative_to(ROOT).as_posix())
        self.assertEqual(offenders, [])

    def test_bootstrap_endpoint_remains_metadata_only(self):
        defaults = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
        self.assertIn(
            'CONFIG_XIAOZHI_OTA_URL="https://api.tenclass.net/xiaozhi/ota/"',
            defaults,
        )
        overlay = (ROOT / "main/cmake/brookesia_xiaozhi_mcp_ownership_overlay.cmake").read_text(encoding="utf-8")
        self.assertIn("info.firmware_version", overlay)
        self.assertNotIn("info.firmware_url", overlay)

    def test_app_event_switch_is_exhaustive_and_routes_update_notice_to_view(self):
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
            r"case AssistantEventKind::UpdateAvailable:\s*_view->setUpdateVersion\(event\.text\.c_str\(\)\);\s*break;",
        )

    def test_assistant_page_renders_exact_noninteractive_notice(self):
        view_h = (ROOT / "main/apps/app_assistant/view/assistant_view.h").read_text(encoding="utf-8")
        view_cpp = (ROOT / "main/apps/app_assistant/view/assistant_view.cpp").read_text(encoding="utf-8")
        app = (ROOT / "main/apps/app_assistant/app_assistant.cpp").read_text(encoding="utf-8")
        self.assertIn("setUpdateVersion", view_h)
        self.assertIn("_update_label", view_h)
        self.assertIn("发现新版本 v", view_cpp)
        self.assertIn("LV_ALIGN_BOTTOM_MID", view_cpp)
        self.assertIn("AssistantEventKind::UpdateAvailable", app)
        self.assertIn("setUpdateVersion(event.text.c_str())", app)
        for forbidden in ("Update now", "立即升级", "esp_restart", "esp_ota_"):
            self.assertNotIn(forbidden, view_cpp + app)

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
        request_start = overlay.index("set(versioned_info_request")
        request_end = overlay.index("]=])", request_start)
        request = overlay[request_start:request_end]

        self.assertIn("stopwatch_duplicate_firmware_version", overlay)
        self.assertIn("stopwatch_capture_firmware_update_session_token", overlay)
        self.assertIn("stopwatch_report_remote_firmware_version", overlay)
        self.assertIn(
            "stopwatch_report_remote_firmware_version(update_notice_session_token, info.firmware_version)",
            overlay,
        )
        self.assertEqual(
            set(re.findall(r"\binfo\.([A-Za-z_]\w*)", request)),
            {"current_version", "firmware_version"},
        )
        for forbidden in (
            r"\binfo\.firmware_url\b",
            r"\bforce\b",
            r"\bdownload\b",
            r"\binstall\b",
            r"\besp_restart\b",
            r"\besp_ota_set_boot_partition\b",
            r"\besp_https_ota\b",
            r"\besp_ota_(?:begin|write|end)\b",
        ):
            self.assertIsNone(re.search(forbidden, request), forbidden)

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
