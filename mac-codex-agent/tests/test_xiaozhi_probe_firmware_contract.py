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
            "acquireConfiguredStaDetailed(RuntimeOwner::Assistant)", runtime
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

    def test_runtime_keeps_pumping_until_forced_app_close_finishes_stop(self):
        main = (ROOT / "main" / "main.cpp").read_text(encoding="utf-8")
        app = (
            ROOT / "main" / "apps" / "app_assistant" / "app_assistant.cpp"
        ).read_text(encoding="utf-8")

        loop = re.search(r"while \(1\) \{(.*?)\n    \}", main, re.DOTALL)
        self.assertIsNotNone(loop)
        self.assertIn("GetRuntimeSupervisor().update(GetHAL().millis());", loop.group(1))
        self.assertLess(
            loop.group(1).find("GetRuntimeSupervisor().update(GetHAL().millis());"),
            loop.group(1).find("GetMooncake().update();"),
        )

        running = re.search(
            r"void AppAssistant::onRunning\(\)\s*\{(.*?)\n\}", app, re.DOTALL
        )
        closing = re.search(
            r"void AppAssistant::onClose\(\)\s*\{(.*?)\n\}", app, re.DOTALL
        )
        self.assertIsNotNone(running)
        self.assertIsNotNone(closing)
        self.assertNotIn("runtime.update", running.group(1))
        self.assertIn("GetRuntimeSupervisor().requestStopAssistant();", closing.group(1))

    def test_assistant_binds_services_for_the_session_lifetime(self):
        runtime = (
            ROOT / "main" / "runtime" / "assistant" / "assistant_runtime.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "std::vector<esp_brookesia::service::ServiceBinding> bindings;",
            runtime,
        )
        self.assertIn("auto binding = manager.bind(std::string(name));", runtime)
        self.assertIn("if (!binding.is_valid())", runtime)
        self.assertIn("bindings.push_back(std::move(binding));", runtime)
        self.assertNotIn("manager.get_service(std::string(name))", runtime)
        self.assertIn("#include <brookesia/service_helper/nvs.hpp>", runtime)
        root_bindings = re.search(
            r"const std::array<std::string_view, 2> names = \{(.*?)\};",
            runtime,
            re.DOTALL,
        )
        self.assertIsNotNone(root_bindings)
        self.assertIn("NVSHelper::get_name()", root_bindings.group(1))
        self.assertIn("ManagerHelper::get_name()", root_bindings.group(1))
        self.assertNotIn("AudioHelper::get_name()", root_bindings.group(1))
        self.assertNotIn("XiaozhiHelper::get_name()", root_bindings.group(1))

    def test_agent_manager_stops_active_agent_before_resetting_its_scheduler(self):
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        overlay = (
            ROOT / "main" / "cmake" / "brookesia_agent_manager_overlay.cmake"
        ).read_text(encoding="utf-8")

        self.assertIn("brookesia_agent_manager_overlay.cmake", root_cmake)
        self.assertIn("apply_brookesia_agent_manager_overlay()", root_cmake)
        complete_block = re.search(
            r"set\(complete_agent_stop \[=\[(.*?)\]=\]\)", overlay, re.DOTALL
        )
        self.assertIsNotNone(complete_block)
        self.assertIn("active_agent_->stop();", complete_block.group(1))
        self.assertLess(
            complete_block.group(1).find("active_agent_->stop();"),
            complete_block.group(1).find("active_agent_.reset();"),
        )

    def test_playback_task_moves_to_psram_before_audio_dependency_starts(self):
        runtime = (
            ROOT / "main" / "runtime" / "assistant" / "assistant_runtime.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("AudioHelper::FunctionId::SetPlaybackConfig", runtime)
        self.assertIn("playback_config.player_task.stack_in_ext = true", runtime)
        self.assertIn("configurePlaybackBeforeStart()", runtime)

        deferred_start_body = re.search(
            r"bool AssistantRuntime::Impl::finishStart\(uint64_t session_id\)\s*\{(.*?)\n\}",
            runtime,
            re.DOTALL,
        )
        self.assertIsNotNone(deferred_start_body)
        self.assertLess(
            deferred_start_body.group(1).find("configurePlaybackBeforeStart()"),
            deferred_start_body.group(1).find("bindServices(session_id)"),
        )

    def test_hardware_report_separates_automated_pass_from_manual_observations(self):
        report = (
            ROOT
            / "docs"
            / "superpowers"
            / "reports"
            / "brookesia-xiaozhi-compatibility.md"
        ).read_text(encoding="utf-8")

        self.assertIn("10-cycle, 20-cycle, and 50-cycle stability", report)
        self.assertIn("Audible TTS | Passed manual", report)
        self.assertIn("50-cycle connected-watch gate is complete", report)
        self.assertIn("Pending manual", report)
        self.assertNotIn("Gate: PASS", report)


if __name__ == "__main__":
    unittest.main()
