import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class DisplayProtectionFirmwareContractTests(unittest.TestCase):
    def test_standard_runner_gates_all_display_protection_host_tests(self):
        runner = (
            ROOT / "scripts" / "run_unified_offline_validation.sh"
        ).read_text(encoding="utf-8")

        expected_titles = (
            "Python contract and tooling tests",
            "Assistant state machine host test",
            "Assistant presentation and lifecycle event policy host tests",
            "Assistant audio level host test",
            "Assistant close controller host test",
            "Assistant event mailbox host test",
            "Assistant update notice host test",
            "Assistant trace host test",
            "Resource supervisor host test",
            "Button feedback policy host test",
            "Shared Wi-Fi policy host test",
            "WiFi settings model host test",
            "WiFi settings controller host test",
            "USB debug command host test",
            "Runtime diagnostics host test",
            "Audio jitter buffer policy host test",
            "Assistant output sync policy host test",
            "Global edge control host test",
            "Status message router host test",
            "Trae status model host test",
            "Trae task carousel host test",
            "Codex status model compatibility host test",
            "Status page swipe and Trae card tap host tests",
            "Display protection configuration host test",
            "AMOLED protection state machine host test",
            "External power tracker host test",
            "Display protection settings model host test",
            "Button input wake gate host test",
            "Assistant display activity host test",
            "Codex status UI host tests",
            "Trae status UI layout host test",
            "Isolated unified candidate clean build and capacity gates",
        )
        stages = re.findall(r'echo "\[(\d+)/(\d+)\] ([^"]+)"', runner)
        self.assertEqual(
            stages,
            [
                (str(number), "32", title)
                for number, title in enumerate(expected_titles, start=1)
            ],
        )

        def stage_body(number):
            marker = f'echo "[{number}/32] '
            start = runner.index(marker)
            end = runner.find('\necho "[', start + len(marker))
            return runner[start:] if end < 0 else runner[start:end]

        strict_compile = '"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain'
        compile_specs = {
            "display_protection_config_test": (
                24,
                (
                    "tests/display_protection_config_test.cpp",
                    "main/runtime/display/display_protection_config.cpp",
                ),
            ),
            "display_protection_logic_test": (
                25,
                (
                    "tests/display_protection_logic_test.cpp",
                    "main/runtime/display/display_protection_logic.cpp",
                ),
            ),
            "display_protection_diagnostics_test": (
                25,
                (
                    "tests/display_protection_diagnostics_test.cpp",
                    "main/runtime/display/display_protection_diagnostics.cpp",
                    "main/runtime/display/display_protection_logic.cpp",
                ),
            ),
            "display_brightness_arbiter_test": (
                25,
                (
                    "tests/display_brightness_arbiter_test.cpp",
                    "main/runtime/display/display_protection_logic.cpp",
                ),
            ),
            "external_power_state_test": (
                26,
                ("tests/external_power_state_test.cpp",),
            ),
            "display_protection_settings_model_test": (
                27,
                (
                    "tests/display_protection_settings_model_test.cpp",
                    "main/runtime/display/display_protection_config.cpp",
                ),
            ),
            "touch_input_gate_test": (
                28,
                (
                    "tests/touch_input_gate_test.cpp",
                    "main/apps/common/edge_control/edge_control_logic.cpp",
                    "main/apps/app_codex_status/status_page_swipe_logic.cpp",
                ),
            ),
        }
        for executable, (number, expected_sources) in compile_specs.items():
            body = stage_body(number)
            output = f'-o "$HOST_TEST_DIR/{executable}"'
            output_index = body.index(output)
            compile_index = body.rfind(strict_compile, 0, output_index)
            self.assertGreaterEqual(compile_index, 0)
            compile_command = body[compile_index : output_index + len(output)]
            sources = re.findall(
                r"^\s+((?:tests|main)/\S+\.cpp) \\$",
                compile_command,
                flags=re.MULTILINE,
            )
            self.assertEqual(sources, list(expected_sources))
            after_output = body[output_index + len(output) :]
            self.assertRegex(
                after_output,
                rf'(?m)^"\$HOST_TEST_DIR/{re.escape(executable)}"$',
            )

    def test_runtime_pmic_driver_accesses_share_one_io_lock(self):
        pmic_source = (ROOT / "main" / "hal" / "hal_pmic.cpp").read_text(
            encoding="utf-8"
        )

        def function_body(signature):
            start = pmic_source.index(signature)
            opening_brace = pmic_source.index("{", start)
            depth = 0
            for index in range(opening_brace, len(pmic_source)):
                if pmic_source[index] == "{":
                    depth += 1
                elif pmic_source[index] == "}":
                    depth -= 1
                    if depth == 0:
                        return pmic_source[start : index + 1]
            self.fail(f"unterminated function: {signature}")

        def call_end(source, name):
            call = re.search(rf"\b{re.escape(name)}\s*\(", source)
            self.assertIsNotNone(call, f"missing call: {name}")
            opening_parenthesis = source.index("(", call.start())
            depth = 0
            for index in range(opening_parenthesis, len(source)):
                if source[index] == "(":
                    depth += 1
                elif source[index] == ")":
                    depth -= 1
                    if depth == 0:
                        terminator = re.match(r"\s*;", source[index + 1 :])
                        self.assertIsNotNone(
                            terminator, f"unterminated call statement: {name}"
                        )
                        return index + 1 + terminator.end()
            self.fail(f"unterminated call: {name}")

        init_body = function_body("void Hal::pmic_init()")
        post_task_start = init_body[call_end(init_body, "xTaskCreate") :]
        self.assertNotRegex(
            post_task_start,
            r"_pm1\s*->\s*\w+\s*\(",
            "direct M5PM1 access after xTaskCreate would race the background task",
        )

        runtime_source = pmic_source.replace(init_body, "")
        runtime_calls = re.findall(r"_pm1->(\w+)\(", runtime_source)
        guarded_calls = re.findall(
            r"with_pmic_io_lock\(\[&\]\s*\{\s*"
            r"return _pm1->(\w+)\(",
            runtime_source,
        )

        self.assertCountEqual(
            runtime_calls,
            guarded_calls,
            "every runtime M5PM1 access must go through with_pmic_io_lock",
        )
        self.assertEqual(
            set(runtime_calls),
            {"readVin", "readVbat", "gpioGetInput", "btnGetState", "setLedEnLevel"},
        )
        self.assertRegex(
            pmic_source,
            r"auto with_pmic_io_lock\(Operation&& operation\)[\s\S]*?"
            r"std::lock_guard<std::mutex> lock\(_pmic_io_mutex\);[\s\S]*?"
            r"return operation\(\);",
        )

    def test_hal_caches_external_power_in_battery_task(self):
        hal_header = (ROOT / "main" / "hal" / "hal.h").read_text(encoding="utf-8")
        pmic_source = (ROOT / "main" / "hal" / "hal_pmic.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("ExternalPowerSnapshot getExternalPowerSnapshot()", hal_header)
        self.assertIn("constexpr uint32_t _power_sample_period_ms = 250", pmic_source)
        self.assertIn("_external_power_tracker.observe", pmic_source)
        self.assertIn("readVin", pmic_source)

    def test_hal_separates_user_and_temporary_brightness(self):
        hal_header = (ROOT / "main" / "hal" / "hal.h").read_text(encoding="utf-8")
        display_source = (ROOT / "main" / "hal" / "hal_display.cpp").read_text(
            encoding="utf-8"
        )
        button_source = (ROOT / "main" / "hal" / "hal_button.cpp").read_text(
            encoding="utf-8"
        )

        for token in (
            "void setTemporaryBackLightBrightness(int brightness)",
            "int getUserBackLightBrightness() const",
            "uint32_t backLightBrightnessRevision() const",
            "ButtonPhysicalState readButtonPhysicalState() const",
            "void suppressButtonInputUntilRelease()",
        ):
            self.assertIn(token, hal_header)

        self.assertIn(
            "_button_input_gate.filter(readButtonPhysicalState())", button_source
        )
        temporary_start = display_source.index(
            "void Hal::setTemporaryBackLightBrightness"
        )
        temporary_body = display_source[
            temporary_start : display_source.index("\n}", temporary_start) + 2
        ]
        self.assertNotIn("Settings settings", temporary_body)

    def test_global_service_precedes_edge_controls_and_apps(self):
        main = (ROOT / "main" / "main.cpp").read_text(encoding="utf-8")
        service = (
            ROOT
            / "main"
            / "runtime"
            / "display"
            / "global_display_protection_service.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("GetGlobalDisplayProtectionService().init()", main)
        protection = main.index("GetGlobalDisplayProtectionService().update()")
        edge = main.index("GetGlobalEdgeControlService().update()")
        mooncake = main.index("GetMooncake().update()")
        self.assertLess(protection, edge)
        self.assertLess(edge, mooncake)
        self.assertIn(
            "GetGlobalDisplayProtectionService().applyPendingHardwareUpdate()", main
        )

        for token in (
            "lv_indev_wait_release(indev)",
            "lv_indev_reset(indev, nullptr)",
            "GetHAL().suppressButtonInputUntilRelease()",
            "GetHAL().setTemporaryBackLightBrightness",
            "GlobalDisplayProtectionService::applyUserBrightness",
            "GlobalDisplayProtectionService::touchInputSuppressed() const",
            "active && GetHAL().getBackLightBrightness() <= 0",
        ):
            self.assertIn(token, service)
        edge_service = (
            ROOT
            / "main"
            / "apps"
            / "common"
            / "edge_control"
            / "global_edge_control_service.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("GetHAL().getUserBackLightBrightness()", edge_service)
        self.assertIn(
            "GetGlobalDisplayProtectionService().applyUserBrightness(",
            edge_service,
        )
        self.assertNotIn("GetHAL().setBackLightBrightness", edge_service)
        app_brightness_writes = []
        for source_path in (ROOT / "main" / "apps").rglob("*.cpp"):
            if "GetHAL().setBackLightBrightness" in source_path.read_text(
                encoding="utf-8"
            ):
                app_brightness_writes.append(source_path.relative_to(ROOT).as_posix())
        self.assertEqual([], app_brightness_writes)

    def test_every_raw_touch_poller_honors_the_global_release_gate(self):
        expected_raw_pollers = {
            "main/apps/common/edge_control/global_edge_control_service.cpp",
            "main/apps/common/status_bar/status_bar.cpp",
            "main/apps/app_launcher/view/view.cpp",
            "main/apps/app_watch_face/view/watch_face_manager.cpp",
            "main/apps/app_codex_status/app_codex_status.cpp",
        }
        actual_raw_pollers = set()
        for source_path in (ROOT / "main" / "apps").rglob("*.cpp"):
            source = source_path.read_text(encoding="utf-8")
            if "lv_indev_get_state" not in source:
                continue
            relative_path = source_path.relative_to(ROOT).as_posix()
            actual_raw_pollers.add(relative_path)
            self.assertIn("touchInputSuppressed()", source, relative_path)
        self.assertEqual(expected_raw_pollers, actual_raw_pollers)

        edge_source = (
            ROOT
            / "main/apps/common/edge_control/global_edge_control_service.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "indev == nullptr || protection.touchInputSuppressed()\n"
            "        ? LV_INDEV_STATE_REL\n"
            "        : lv_indev_get_state(indev)",
            edge_source,
        )

        service = (
            ROOT
            / "main"
            / "runtime"
            / "display"
            / "global_display_protection_service.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("_touch_input_gate.beginFrame", service)
        self.assertIn("_touch_input_gate.suppressUntilRelease()", service)

        codex_header = (
            ROOT / "main" / "apps" / "app_codex_status" / "app_codex_status.h"
        ).read_text(encoding="utf-8")
        swipe_header = (
            ROOT
            / "main"
            / "apps"
            / "app_codex_status"
            / "status_page_swipe_logic.h"
        ).read_text(encoding="utf-8")
        self.assertNotIn("StatusPageTouchWakeGate", codex_header + swipe_header)

    def test_global_service_owns_configuration_and_power_policy(self):
        service_header = (
            ROOT
            / "main"
            / "runtime"
            / "display"
            / "global_display_protection_service.h"
        ).read_text(encoding="utf-8")
        service_source = (
            ROOT
            / "main"
            / "runtime"
            / "display"
            / "global_display_protection_service.cpp"
        ).read_text(encoding="utf-8")
        service = service_header + service_source

        for token in (
            'Settings settings("disp_guard")',
            'GetInt("dim_idx"',
            'GetInt("off_idx"',
            'GetBool("chg_guard"',
            'GetBool("dim_on", defaults.dim_enabled)',
            'GetBool("off_on", defaults.screen_off_enabled)',
            'SetInt("dim_idx"',
            'SetInt("off_idx"',
            'SetBool("chg_guard"',
            'SetBool("dim_on", sanitized.dim_enabled)',
            'SetBool("off_on", sanitized.screen_off_enabled)',
            "DisplayProtectionPolicy{",
            "getExternalPowerSnapshot()",
            "screenOffNow()",
            "applyConfig(const DisplayProtectionConfig& config, bool persist)",
        ):
            self.assertIn(token, service)

        nvs_keys = re.findall(
            r'(?:GetInt|GetBool|SetInt|SetBool)\("([^"]+)"', service_source
        )
        self.assertEqual(
            {"dim_idx", "off_idx", "chg_guard", "dim_on", "off_on"},
            set(nvs_keys),
        )
        for key in nvs_keys:
            self.assertLessEqual(len(key), 15)

        self.assertIn(
            "_config.dim_enabled || _config.screen_off_enabled", service
        )
        self.assertRegex(
            service,
            r"_config\.protect_while_charging \|\|\s*"
            r"_external_power\.state != ExternalPowerState::ExternalPower",
        )
        self.assertIn(
            "_logic.automaticEnabled() && _logic.reasonMask() == 0", service
        )
        self.assertIn("DisplayWakeReason::KeepAwakeReleased", service_source)

    def test_codex_delegates_display_protection_to_global_service(self):
        codex_header = (
            ROOT / "main" / "apps" / "app_codex_status" / "app_codex_status.h"
        ).read_text(encoding="utf-8")
        codex_source = (
            ROOT / "main" / "apps" / "app_codex_status" / "app_codex_status.cpp"
        ).read_text(encoding="utf-8")
        codex_source_or_header = codex_header + codex_source

        for forbidden in (
            "kPowerPolicyCheckMs",
            "isBatteryCharging(false)",
            "setAutomaticProtection(",
            "_external_power_connected",
            "_power_policy_known",
            "_screen_off_restore_brightness",
        ):
            self.assertNotIn(forbidden, codex_source_or_header)

        self.assertIn(
            "GetGlobalDisplayProtectionService().screenOffNow()", codex_source
        )
        self.assertIn(
            "const bool screen_off =\n"
            "        GetGlobalDisplayProtectionService().snapshot().state ==\n"
            "        DisplayProtectionState::ScreenOff;\n"
            "    _pending_glow->set_state(_alert_carousel.display_state(), screen_off);",
            codex_source,
        )

        service_header = (
            ROOT
            / "main"
            / "runtime"
            / "display"
            / "global_display_protection_service.h"
        ).read_text(encoding="utf-8")
        service_source = (
            ROOT
            / "main"
            / "runtime"
            / "display"
            / "global_display_protection_service.cpp"
        ).read_text(encoding="utf-8")
        self.assertNotIn(
            "setAutomaticProtection", service_header + service_source
        )

    def test_assistant_publishes_semantic_activity_only(self):
        assistant_source = (
            ROOT / "main" / "apps" / "app_assistant" / "app_assistant.cpp"
        ).read_text(encoding="utf-8")
        tracker = (
            ROOT
            / "main"
            / "apps"
            / "app_assistant"
            / "assistant_display_activity.h"
        ).read_text(encoding="utf-8")

        for token in (
            "AssistantDisplayActivitySignal::WakeDetected",
            "AssistantDisplayActivitySignal::Listening",
            "AssistantDisplayActivitySignal::UserText",
            "AssistantDisplayActivitySignal::AgentText",
            "AssistantDisplayActivitySignal::Emotion",
            "AssistantDisplayActivitySignal::Speaking",
            "AssistantDisplayActivitySignal::Idle",
            "DisplayKeepAwakeReason::AssistantInteraction",
        ):
            self.assertIn(token, assistant_source)
        self.assertIn("StaleAfterMs = 60000", tracker)
        self.assertNotIn("_view->update", tracker)

    def test_stopwatch_model_and_running_hold_outlive_subpage(self):
        clock_header = (
            ROOT / "main" / "apps" / "app_clock" / "app_clock.h"
        ).read_text(encoding="utf-8")
        clock_source = (
            ROOT / "main" / "apps" / "app_clock" / "app_clock.cpp"
        ).read_text(encoding="utf-8")
        stopwatch_view_header = (
            ROOT / "main" / "apps" / "app_stopwatch" / "view" / "view.h"
        ).read_text(encoding="utf-8")

        self.assertIn("std::unique_ptr<model::Stopwatch> _stopwatch", clock_header)
        self.assertNotIn(
            "std::unique_ptr<model::Stopwatch> _stopwatch", stopwatch_view_header
        )
        self.assertIn("model::Stopwatch* _stopwatch", stopwatch_view_header)
        self.assertIn(
            "void init(lv_obj_t* parent, model::Stopwatch& stopwatch)",
            stopwatch_view_header,
        )
        for token in (
            "DisplayKeepAwakeReason::StopwatchRunning",
            "model::Stopwatch::State_t::Running",
            "setKeepAwake",
        ):
            self.assertIn(token, clock_source)

    def test_alarm_wakes_before_presenting_and_releases_after_dismissal(self):
        clock_source = (
            ROOT / "main" / "apps" / "app_clock" / "app_clock.cpp"
        ).read_text(encoding="utf-8")
        callback_start = clock_source.index("_alarm_clock->onTriggered().connect")
        callback_end = clock_source.index("\n    });", callback_start)
        callback = clock_source[callback_start:callback_end]
        destroy_start = clock_source.index("void AppClock::onDestroy()")
        destroy_end = clock_source.index("void AppClock::transitionTo", destroy_start)
        destroy_body = clock_source[destroy_start:destroy_end]

        acquire = callback.index("DisplayKeepAwakeReason::AlarmRinging, true")
        flush = callback.index("applyPendingHardwareUpdate()")
        view = callback.index("std::make_unique<view::AlarmTriggerView>()")
        release = callback.index("DisplayKeepAwakeReason::AlarmRinging, false")
        self.assertLess(acquire, flush)
        self.assertLess(flush, view)
        self.assertLess(view, release)
        self.assertIn(
            "DisplayKeepAwakeReason::AlarmRinging, false", destroy_body
        )


if __name__ == "__main__":
    unittest.main()
