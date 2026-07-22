import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class AssistantMemoryPolicyContractTests(unittest.TestCase):
    def test_xiaozhi_chat_owns_and_releases_the_session_mcp_engine(self):
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        overlay = (
            ROOT / "main" / "cmake" / "brookesia_xiaozhi_mcp_ownership_overlay.cmake"
        ).read_text(encoding="utf-8")

        self.assertIn("brookesia_xiaozhi_mcp_ownership_overlay.cmake", root_cmake)
        self.assertIn("apply_brookesia_xiaozhi_mcp_ownership_overlay()", root_cmake)
        self.assertIn("chat_config.owns_mcp_engine = true;", overlay)
        self.assertIn("list(REMOVE_ITEM xiaozhi_agent_sources", overlay)
        self.assertIn("target_sources(${xiaozhi_agent_lib}", overlay)

    def test_xiaozhi_mqtt_reconnect_poll_cannot_block_two_second_close_gate(self):
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        source = (ROOT / "main" / "cmake" / "xiaozhi_mqtt_stop_overlay.cmake").read_text(
            encoding="utf-8"
        )

        self.assertIn("apply_xiaozhi_mqtt_stop_overlay()", root_cmake)
        match = re.search(r"\.reconnect_timeout_ms\s*=\s*(\d+)", source)
        self.assertIsNotNone(match)
        self.assertLessEqual(int(match.group(1)), 1000)

    def test_radio_bss_and_general_allocations_preserve_internal_dma_memory(self):
        expected = (
            "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y",
            "CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0",
            "CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768",
        )
        for relative_path in ("sdkconfig.defaults", "sdkconfig"):
            config = (ROOT / relative_path).read_text(encoding="utf-8")
            for setting in expected:
                self.assertIn(setting, config, f"{setting} missing from {relative_path}")

    def test_light_audio_worker_uses_psram_for_its_24k_stack(self):
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        overlay = (
            ROOT / "main" / "cmake" / "audio_processor_light_overlay.cmake"
        ).read_text(encoding="utf-8")

        self.assertIn("audio_processor_light_overlay.cmake", root_cmake)
        self.assertIn("apply_audio_processor_light_overlay()", root_cmake)
        self.assertIn("xTaskCreatePinnedToCoreWithCaps", overlay)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", overlay)
        self.assertIn("vTaskDeleteWithCaps(NULL)", overlay)
        self.assertIn("list(REMOVE_ITEM audio_processor_sources", overlay)
        self.assertIn("target_sources(${audio_processor_lib}", overlay)

    def test_brookesia_audio_service_adapts_to_the_light_processor_contract(self):
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        overlay = (
            ROOT / "main" / "cmake" / "brookesia_audio_light_overlay.cmake"
        ).read_text(encoding="utf-8")

        self.assertIn("brookesia_audio_light_overlay.cmake", root_cmake)
        self.assertIn("apply_brookesia_audio_light_overlay()", root_cmake)
        self.assertIn("CONFIG_AUDIO_PROCESSOR_IMPL_LIGHT", overlay)
        self.assertIn("DEFAULT_AV_PROCESSOR_CUSTOM_CONFIG", overlay)
        self.assertIn("audio_recorder_wn_event_t", overlay)
        self.assertIn("Helper::AFE_Event::WakeStart", overlay)
        self.assertIn("light implementation writes feeder PCM directly", overlay)
        self.assertIn("encoder_lifecycle_mutex", overlay)
        self.assertIn("decoder_lifecycle_mutex", overlay)
        self.assertIn("encoder_fetch_stop_requested", overlay)
        self.assertIn("std::atomic<bool>", overlay)
        self.assertIn("std::lock_guard<std::mutex>", overlay)
        self.assertIn("Stop dependants before closing their shared playback", overlay)
        self.assertIn("while (!play_url_queue_.empty())", overlay)
        self.assertIn("reallocating_queue_clear", overlay)
        self.assertIn("stable_queue_clear", overlay)
        self.assertIn("list(REMOVE_ITEM service_audio_sources", overlay)
        self.assertIn("list(REMOVE_ITEM type_converter_sources", overlay)

    def test_agent_stop_clears_action_queue_without_reallocating_it(self):
        overlay = (
            ROOT / "main" / "cmake" / "brookesia_agent_manager_overlay.cmake"
        ).read_text(encoding="utf-8")

        self.assertIn("upstream_state_machine", overlay)
        self.assertIn("while (!general_action_queue_.empty())", overlay)
        self.assertIn("reallocating_action_queue_clear", overlay)
        self.assertIn("stable_action_queue_clear", overlay)

    def test_wifi_event_loop_has_stack_for_scan_and_connect_callbacks(self):
        setting = "CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096"
        for relative_path in ("sdkconfig.defaults", "sdkconfig"):
            config = (ROOT / relative_path).read_text(encoding="utf-8")
            self.assertIn(setting, config, f"{setting} missing from {relative_path}")

    def test_low_bandwidth_wifi_keeps_static_rx_at_the_ba_window(self):
        expected = (
            "CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6",
            "CONFIG_ESP_WIFI_RX_BA_WIN=6",
        )
        for relative_path in ("sdkconfig.defaults", "sdkconfig"):
            config = (ROOT / relative_path).read_text(encoding="utf-8")
            for setting in expected:
                self.assertIn(setting, config, f"{setting} missing from {relative_path}")

    def test_tls_large_buffers_can_fall_back_to_psram(self):
        for relative_path in ("sdkconfig.defaults", "sdkconfig"):
            config = (ROOT / relative_path).read_text(encoding="utf-8")
            self.assertIn(
                "CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC=y",
                config,
                f"default mbedTLS allocation missing from {relative_path}",
            )
            self.assertNotIn(
                "CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y",
                config,
                f"mbedTLS remains internal-only in {relative_path}",
            )

    def test_brookesia_idle_poll_avoids_allocating_sleep_primitives(self):
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        overlay = (
            ROOT / "main" / "cmake" / "brookesia_task_scheduler_overlay.cmake"
        ).read_text(encoding="utf-8")

        self.assertIn("brookesia_task_scheduler_overlay.cmake", root_cmake)
        self.assertIn(
            "boost::this_thread::sleep_for(boost::chrono::milliseconds(poll_interval_ms))",
            overlay,
        )
        self.assertIn("vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));", overlay)
        self.assertIn("#if defined(ESP_PLATFORM)", overlay)
        self.assertIn("#include \"freertos/task.h\"", overlay)
        self.assertNotIn("no_interruption_point::sleep_for", overlay)
        self.assertIn("list(REMOVE_ITEM scheduler_sources", overlay)
        self.assertIn("target_sources(${brookesia_lib}", overlay)

    def test_runtime_diagnostics_report_largest_internal_block(self):
        header = (ROOT / "main" / "runtime" / "runtime_diagnostics.h").read_text(
            encoding="utf-8"
        )
        implementation = (
            ROOT / "main" / "runtime" / "runtime_diagnostics.cpp"
        ).read_text(encoding="utf-8")
        supervisor = (
            ROOT / "main" / "runtime" / "runtime_supervisor.cpp"
        ).read_text(encoding="utf-8")
        assistant = (
            ROOT / "main" / "runtime" / "assistant" / "assistant_runtime.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("internal_largest_free_block", header)
        self.assertIn(
            "heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)", implementation
        )
        self.assertIn("largest={}", supervisor)
        for boundary in (
            '"before-resources"',
            '"after-wifi"',
            '"after-service-setup"',
            '"setup-exception"',
        ):
            self.assertIn(boundary, assistant)

    def test_assistant_allocation_exceptions_roll_back_instead_of_aborting(self):
        runtime = (
            ROOT / "main" / "runtime" / "assistant" / "assistant_runtime.cpp"
        ).read_text(encoding="utf-8")

        start_body = re.search(
            r"AssistantStartResult AssistantRuntime::Impl::start\(\)\s*\{(.*?)\n\}",
            runtime,
            re.DOTALL,
        )
        self.assertIsNotNone(start_body)
        self.assertIn("catch (const std::exception& error)", start_body.group(1))
        self.assertIn("catch (...)", start_body.group(1))
        self.assertIn("failAndReturn(", start_body.group(1))

        submit_body = re.search(
            r"bool AssistantRuntime::Impl::submitAction\(.*?\)\s*\{(.*?)\n\}",
            runtime,
            re.DOTALL,
        )
        self.assertIsNotNone(submit_body)
        self.assertIn("catch (const std::exception& error)", submit_body.group(1))
        self.assertIn("catch (...)", submit_body.group(1))

    def test_assistant_defers_heavy_services_until_wifi_has_an_ip(self):
        runtime = (
            ROOT / "main" / "runtime" / "assistant" / "assistant_runtime.cpp"
        ).read_text(encoding="utf-8")

        start_body = re.search(
            r"AssistantStartResult AssistantRuntime::Impl::start\(\)\s*\{(.*?)\n\}",
            runtime,
            re.DOTALL,
        )
        update_body = re.search(
            r"void AssistantRuntime::Impl::update\(uint32_t now_ms\)\s*\{(.*?)\n\}",
            runtime,
            re.DOTALL,
        )
        self.assertIsNotNone(start_body)
        self.assertIsNotNone(update_body)
        self.assertIn("service_setup_pending = true", start_body.group(1))
        self.assertIn('"Connecting"', start_body.group(1))
        self.assertNotIn("bindServices(session)", start_body.group(1))
        self.assertIn("GetSharedWifiService().staConnected()", update_body.group(1))
        self.assertIn("finishStart(session)", update_body.group(1))
        self.assertIn("service_setup_pending", update_body.group(1))


if __name__ == "__main__":
    unittest.main()
