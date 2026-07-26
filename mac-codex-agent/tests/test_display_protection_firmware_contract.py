import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class DisplayProtectionFirmwareContractTests(unittest.TestCase):
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
            "active && GetHAL().getBackLightBrightness() <= 0",
        ):
            self.assertIn(token, service)
        self.assertNotIn("setBackLightBrightness", service)
        edge_service = (
            ROOT
            / "main"
            / "apps"
            / "common"
            / "edge_control"
            / "global_edge_control_service.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("GetHAL().getUserBackLightBrightness()", edge_service)

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
            'SetInt("dim_idx"',
            'SetInt("off_idx"',
            'SetBool("chg_guard"',
            "getExternalPowerSnapshot()",
            "screenOffNow()",
            "applyConfig(const DisplayProtectionConfig& config, bool persist)",
        ):
            self.assertIn(token, service)

        nvs_keys = re.findall(
            r'(?:GetInt|GetBool|SetInt|SetBool)\("([^"]+)"', service_source
        )
        self.assertEqual({"dim_idx", "off_idx", "chg_guard"}, set(nvs_keys))
        for key in nvs_keys:
            self.assertLessEqual(len(key), 15)

        self.assertIn(
            "_config.protect_while_charging || "
            "_external_power.state != ExternalPowerState::ExternalPower",
            service,
        )
        self.assertIn(
            "_logic.automaticEnabled() && _logic.reasonMask() == 0", service
        )

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
