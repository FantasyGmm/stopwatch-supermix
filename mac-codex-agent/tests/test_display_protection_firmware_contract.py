import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class DisplayProtectionFirmwareContractTests(unittest.TestCase):
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

    def test_codex_policy_tracks_external_power_and_preserves_manual_off(self):
        codex_source = (
            ROOT / "main" / "apps" / "app_codex_status" / "app_codex_status.cpp"
        ).read_text(encoding="utf-8")

        for token in (
            "kPowerPolicyCheckMs = 1000",
            "GetHAL().isBatteryCharging(false)",
            "setAutomaticProtection(!external_power)",
            "refresh_display_protection_policy(now, true)",
            "refresh_display_protection_policy(now, false)",
        ):
            self.assertIn(token, codex_source)

        close_body = codex_source[
            codex_source.index("void AppCodexStatus::onClose()") :
            codex_source.index("void AppCodexStatus::create_view()")
        ]
        manual_off_body = codex_source[
            codex_source.index("void AppCodexStatus::enter_screen_off()") :
            codex_source.index("void AppCodexStatus::wake_screen()")
        ]
        wake_body = codex_source[
            codex_source.index("void AppCodexStatus::wake_screen()") :
            codex_source.index("void AppCodexStatus::set_percent")
        ]
        self.assertIn("setAutomaticProtection(true)", close_body)
        self.assertIn("setAutomaticProtection(false, false)", manual_off_body)
        self.assertIn("setTemporaryBackLightBrightness(0)", manual_off_body)
        self.assertIn("setTemporaryBackLightBrightness", wake_body)

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
        update_start = clock_source.index("bool AppClock::updateAlarmTrigger()")
        clear_start = clock_source.index("void AppClock::clearAlarmTrigger()", update_start)
        update_body = clock_source[update_start:clear_start]
        destroy_end = update_start
        destroy_body = clock_source[destroy_start:destroy_end]

        acquire = callback.index("DisplayKeepAwakeReason::AlarmRinging, true")
        flush = callback.index("applyPendingHardwareUpdate()")
        view = callback.index("std::make_unique<view::AlarmTriggerView>()")
        self.assertLess(acquire, flush)
        self.assertLess(flush, view)
        self.assertIn(
            "DisplayKeepAwakeReason::AlarmRinging, false", update_body
        )
        self.assertIn(
            "DisplayKeepAwakeReason::AlarmRinging, false", destroy_body
        )


if __name__ == "__main__":
    unittest.main()
