import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class XiaozhiProbeFirmwareContractTests(unittest.TestCase):
    def test_agent_dependencies_are_pinned_to_zero_seven(self):
        manifest = (ROOT / "main" / "idf_component.yml").read_text(
            encoding="utf-8"
        )
        lock = (ROOT / "dependencies.lock").read_text(encoding="utf-8")
        for component in (
            "espressif/brookesia_agent_manager",
            "espressif/brookesia_agent_xiaozhi",
            "espressif/brookesia_service_nvs",
            "espressif/brookesia_service_sntp",
        ):
            self.assertRegex(
                manifest,
                re.compile(
                    rf"(?m)^\s{{2}}{re.escape(component)}:\s*\n"
                    rf"\s+version:\s*['\"]?>=0\.7\.0,<0\.8\.0['\"]?\s*$"
                ),
                component,
            )
            lock_block = re.search(
                rf"(?ms)^  {re.escape(component)}:\n(.*?)(?=^  \S|\Z)", lock
            )
            self.assertIsNotNone(lock_block, component)
            self.assertRegex(
                lock_block.group(1),
                re.compile(r"(?m)^    version: 0\.7\.\d+$"),
                component,
            )

    def test_unified_assets_use_the_existing_wake_model_without_ui_assets(self):
        kconfig = (ROOT / "main" / "Kconfig.projbuild").read_text(
            encoding="utf-8"
        )
        defaults = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
        assets_cmake = (ROOT / "main" / "cmake" / "unified_assets.cmake").read_text(
            encoding="utf-8"
        )
        main_cmake = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("config LANGUAGE_ZH_CN", kconfig)
        self.assertIn("config USE_AFE_WAKE_WORD", kconfig)
        self.assertIn("CONFIG_LANGUAGE_ZH_CN=y", defaults)
        self.assertIn("CONFIG_USE_AFE_WAKE_WORD=y", defaults)
        self.assertIn("CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y", defaults)
        self.assertIn(
            'CONFIG_XIAOZHI_OTA_URL="https://api.tenclass.net/xiaozhi/ota/"',
            defaults,
        )
        self.assertIn('CONFIG_BROOKESIA_AGENT_XIAOZHI_WAKE_WORD="\u4f60\u597d\u5c0f\u667a"', defaults)
        self.assertIn("# CONFIG_MCP_TRANSPORT_HTTP_SERVER is not set", defaults)
        self.assertIn("# CONFIG_MCP_TRANSPORT_HTTP_CLIENT is not set", defaults)

        self.assertIn("build_default_assets.py", assets_cmake)
        self.assertIn('--sdkconfig" "${SDKCONFIG}', assets_cmake)
        self.assertIn("managed_components/espressif__esp-sr/model", assets_cmake)
        self.assertIn("generated_unified_assets ALL", assets_cmake)
        self.assertIn("4194304", assets_cmake)
        self.assertNotIn("--builtin_text_font", assets_cmake)
        self.assertNotIn("--emoji_collection", assets_cmake)
        self.assertIn("include(cmake/unified_assets.cmake)", main_cmake)

    def test_production_runtime_owns_async_lifecycle_and_probe_is_thin(self):
        runtime = (
            ROOT / "main" / "runtime" / "assistant" / "assistant_runtime.cpp"
        ).read_text(encoding="utf-8")
        probe = (
            ROOT / "main" / "runtime" / "assistant" / "assistant_probe.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("AssistantStateMachine", runtime)
        self.assertIn("AssistantEventMailbox", runtime)
        self.assertIn(
            "GetSharedWifiService().acquireSta(RuntimeOwner::Assistant)", runtime
        )
        self.assertIn("acquireCapture(RuntimeOwner::Assistant)", runtime)
        self.assertIn("acquirePlayback(RuntimeOwner::Assistant)", runtime)
        self.assertIn("AudioHelper::FunctionId::SetAFE_Config", runtime)
        self.assertIn('model_partition_label = "assets"', runtime)
        self.assertIn("ManagerHelper::FunctionId::SetAgentInfo", runtime)
        self.assertIn("ManagerHelper::FunctionId::SetTargetAgent", runtime)
        self.assertIn("ManagerHelper::EventId::GeneralEventHappened", runtime)
        self.assertIn("XiaozhiHelper::EventId::ActivationCodeReceived", runtime)
        self.assertIn("ManagerHelper::EventId::UserSpeakingTextGot", runtime)
        self.assertIn("ManagerHelper::EventId::AgentSpeakingTextGot", runtime)
        self.assertIn("ManagerHelper::EventId::EmoteGot", runtime)
        self.assertIn("ManagerHelper::GeneralAction::Activate", runtime)
        self.assertIn("ManagerHelper::GeneralAction::Start", runtime)
        self.assertIn("ManagerHelper::GeneralAction::Stop", runtime)
        self.assertIn("ManagerHelper::GeneralEvent::Stopped", runtime)
        self.assertIn("call_function_async", runtime)
        self.assertIn("disconnect", runtime)
        self.assertIn("stop_started_ms = GetHAL().millis()", runtime)
        self.assertNotIn("stop_started_ms = last_update_ms", runtime)
        self.assertIn("GetAssistantRuntime()", probe)
        self.assertNotIn("std::vector<std::shared_ptr", probe)

    def test_hardware_report_keeps_the_gate_pending(self):
        report = (
            ROOT
            / "docs"
            / "superpowers"
            / "reports"
            / "brookesia-xiaozhi-compatibility.md"
        ).read_text(encoding="utf-8")

        self.assertIn("Pending hardware", report)
        self.assertNotIn("Gate: PASS", report)


if __name__ == "__main__":
    unittest.main()
