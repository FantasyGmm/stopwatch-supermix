import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class LowBatteryAlertFirmwareContractTests(unittest.TestCase):
    def test_unified_runner_includes_global_low_battery_stage(self):
        runner = (ROOT / "scripts" / "run_unified_offline_validation.sh").read_text()
        self.assertIn("[19/21] Global low-battery alert host test", runner)
        self.assertIn("tests/low_battery_alert_logic_test.cpp", runner)
        self.assertIn("main/runtime/power/low_battery_alert_logic.cpp", runner)
        self.assertIn('"$HOST_TEST_DIR/low_battery_alert_logic_test"', runner)

    def test_global_service_and_main_loop_contract(self):
        service_header_path = (
            ROOT / "main" / "runtime" / "power" / "global_low_battery_alert_service.h"
        )
        service_source_path = (
            ROOT / "main" / "runtime" / "power" / "global_low_battery_alert_service.cpp"
        )
        self.assertTrue(service_header_path.exists(), "global alert service header is missing")
        self.assertTrue(service_source_path.exists(), "global alert service source is missing")

        service_header = service_header_path.read_text(encoding="utf-8")
        service_source = service_source_path.read_text(encoding="utf-8")
        service = service_header + service_source
        for token in (
            "LowBatteryAlertLogic _logic",
            "LowBatteryAlertOverlay _overlay",
            "GetHAL().batterySampleSnapshot()",
            "battery_sample.revision != _last_battery_revision",
            ".battery_percent = static_cast<uint8_t>(sample_due ? battery_sample.level : 0),",
            "GetHAL().isBatteryCharging(false)",
            "GetHAL().isDisplayVisible()",
            "GetHAL().isAudioPlaying()",
            "RuntimeOwner::System",
            "audio::play_melody",
            "_logic.deferSound()",
        ):
            self.assertIn(token, service)
        self.assertNotIn("GetHAL().batterySampleRevision()", service_source)
        self.assertNotIn("GetHAL().getBatteryLevel()", service_source)
        self.assertIn(
            "static constexpr uint32_t chargingPollIntervalMs = 1000;",
            service_header,
        )
        for token in (
            "const bool display_woke = display_visible && !_last_display_visible;",
            "!_charging_observed || sample_due || display_woke",
            "static_cast<uint32_t>(now_ms - _last_charging_poll_ms) >=",
            "chargingPollIntervalMs",
            "if (charging_refresh_due)",
            "_external_power = GetHAL().isBatteryCharging(false);",
            ".external_power = _external_power,",
        ):
            self.assertIn(token, service)
        self.assertEqual(
            service_source.count("GetHAL().isBatteryCharging(false)"), 2
        )
        self.assertLess(
            service_source.index("if (charging_refresh_due)"),
            service_source.index("_logic.update(input)"),
        )
        self.assertNotIn(
            ".external_power = GetHAL().isBatteryCharging(false),", service_source
        )
        self.assertNotIn(
            ".external_power = sample_due && GetHAL().isBatteryCharging(false),",
            service_source,
        )

        main = (ROOT / "main" / "main.cpp").read_text(encoding="utf-8")
        policy = main.index("GetGlobalLowBatteryAlertService().update()")
        overlay = main.index("GetGlobalLowBatteryAlertService().updateOverlay()")
        edge = main.index("GetGlobalEdgeControlService().update()")
        audio = main.index("GetGlobalLowBatteryAlertService().applyPendingAudio()")
        mooncake = main.index("GetMooncake().update()")
        self.assertLess(policy, overlay)
        self.assertLess(overlay, edge)
        self.assertLess(edge, audio)
        self.assertLess(audio, mooncake)

        setup = (ROOT / "main" / "apps" / "app_setup" / "app_setup.cpp").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("GetGlobalLowBatteryAlertService", setup)

    def test_overlay_uses_circular_safe_layout_and_remains_presentation_only(self):
        layout = (ROOT / "main" / "apps" / "common" / "low_battery" / "low_battery_alert_layout.h").read_text(
            encoding="utf-8"
        )
        overlay_header = (
            ROOT / "main" / "apps" / "common" / "low_battery" / "low_battery_alert_overlay.h"
        ).read_text(encoding="utf-8")
        overlay_source = (
            ROOT / "main" / "apps" / "common" / "low_battery" / "low_battery_alert_overlay.cpp"
        ).read_text(encoding="utf-8")

        for token in ("screenSize = 466", "safeDiameter = 420", "bannerWidth = 260", "bannerHeight = 96"):
            self.assertIn(token, layout)
        for token in (
            "font_puhui_14_1", "MontserratSemiBold26", "电量低", "请及时充电",
            "moveForeground()",
        ):
            self.assertIn(token, overlay_source)
        self.assertIn("_title->setTextColor(lv_color_white())", overlay_source)
        self.assertIn("_message->setTextColor(lv_color_white())", overlay_source)

        objects = ("root", "banner", "title", "percent", "message")
        for index, name in enumerate(objects):
            start = overlay_source.index(f"_{name} = std::make_unique")
            end = (
                overlay_source.index(f"_{objects[index + 1]} = std::make_unique", start)
                if index + 1 < len(objects)
                else overlay_source.index("return true;", start)
            )
            initialization = overlay_source[start:end]
            self.assertIn(f"_{name}->removeFlag(LV_OBJ_FLAG_CLICKABLE)", initialization)
            self.assertIn(f"_{name}->removeFlag(LV_OBJ_FLAG_SCROLLABLE)", initialization)

        self.assertNotIn("GetHAL()", overlay_header + overlay_source)

    def test_global_service_keeps_running_through_guide_and_alarm(self):
        launcher = (ROOT / "main" / "apps" / "app_launcher" / "app_launcher.cpp").read_text(
            encoding="utf-8"
        )
        guide_start = launcher.index("void AppLauncher::show_guide_page()")
        guide_body = launcher[guide_start:]
        self.assertNotIn("while (1)", guide_body)
        self.assertNotIn("while (true)", guide_body)
        self.assertIn("_guide_page", launcher)
        self.assertIn("update_guide_page()", launcher)

        clock = (ROOT / "main" / "apps" / "app_clock" / "app_clock.cpp").read_text(
            encoding="utf-8"
        )
        trigger_start = clock.index("_alarm_clock->onTriggered()")
        trigger_end = clock.index("void AppClock::onOpen()", trigger_start)
        trigger_callback = clock[trigger_start:trigger_end]
        self.assertNotIn("while (true)", trigger_callback)
        self.assertNotIn("while (1)", trigger_callback)
        self.assertIn("_alarm_trigger_view", clock)
        self.assertIn("updateAlarmTrigger()", clock)

    def test_alarm_owns_playback_and_audio_delivery_refreshes_external_power(self):
        supervisor = (ROOT / "main" / "runtime" / "resource_supervisor.h").read_text(
            encoding="utf-8"
        )
        alarm = (ROOT / "main" / "hal" / "hal_alarm.cpp").read_text(
            encoding="utf-8"
        )
        service = (
            ROOT / "main" / "runtime" / "power" / "global_low_battery_alert_service.cpp"
        ).read_text(encoding="utf-8")

        self.assertRegex(supervisor, r"enum class RuntimeOwner \{[^}]*Alarm")
        self.assertIn("acquirePlayback(RuntimeOwner::Alarm)", alarm)
        self.assertIn("releasePlayback(RuntimeOwner::Alarm)", alarm)
        self.assertIn("_playback_owned", alarm)
        self.assertIn("_task_handle != nullptr && !already_active", alarm)
        self.assertNotIn("playback_acquired", alarm)
        self.assertNotIn('"alarm already active"', alarm)
        stop_start = alarm.index("void stop()")
        stop_end = alarm.index("private:", stop_start)
        stop_body = alarm[stop_start:stop_end]
        owned_block = stop_body.index("if (playback_owned)")
        self.assertGreater(stop_body.index("GetHAL().audioPlay"), owned_block)
        self.assertGreater(
            stop_body.index("releasePlayback(RuntimeOwner::Alarm)"),
            owned_block,
        )
        task_start = alarm.index("void task()")
        task_end = alarm.index("std::mutex _mutex", task_start)
        task_body = alarm[task_start:task_end]
        self.assertNotIn("bool playback_owned", task_body)
        task_lock = task_body.index("std::lock_guard<std::mutex> lock(_mutex)")
        owned_check = task_body.index("if (_playback_owned)")
        tone_submit = task_body.index("audio::play_tone")
        self.assertLess(task_lock, owned_check)
        self.assertLess(owned_check, tone_submit)

        apply_start = service.index("void GlobalLowBatteryAlertService::applyPendingAudio()")
        apply_end = service.index("void GlobalLowBatteryAlertService::shutdown()", apply_start)
        apply_audio = service[apply_start:apply_end]
        power_refresh = apply_audio.index("GetHAL().isBatteryCharging(false)")
        clear_pending = apply_audio.index("_audio_start_pending = false")
        play_sound = apply_audio.index("audio::play_melody")
        self.assertLess(power_refresh, clear_pending)
        self.assertLess(power_refresh, play_sound)
        self.assertIn("_logic.update", apply_audio[power_refresh:play_sound])

    def test_hal_exposes_low_battery_observations(self):
        header = (ROOT / "main" / "hal" / "hal.h").read_text(encoding="utf-8")
        pmic = (ROOT / "main" / "hal" / "hal_pmic.cpp").read_text(encoding="utf-8")
        display = (ROOT / "main" / "hal" / "hal_display.cpp").read_text(encoding="utf-8")
        audio = (ROOT / "main" / "hal" / "hal_audio.cpp").read_text(encoding="utf-8")

        for token in (
            "struct BatterySampleSnapshot",
            "batterySampleSnapshot()",
            "batterySampleRevision()",
            "isDisplayVisible()",
            "isAudioPlaying()",
        ):
            self.assertIn(token, header)
        self.assertIn("_bat_sample_revision++", pmic)
        self.assertIn("BatterySampleSnapshot Hal::batterySampleSnapshot()", pmic)
        snapshot_getter = pmic[
            pmic.index("BatterySampleSnapshot Hal::batterySampleSnapshot()") :
            pmic.index("uint32_t Hal::batterySampleRevision()")
        ]
        self.assertEqual(snapshot_getter.count("lock(_bat_level_mutex)"), 1)
        self.assertIn("return {_bat_level, _bat_sample_revision};", snapshot_getter)
        self.assertIn("uint32_t Hal::batterySampleRevision()", pmic)
        self.assertIn("bool Hal::isDisplayVisible()", display)
        self.assertIn("return _bl_brightness > 0", display)
        self.assertIn("bool isPlaying()", audio)
        self.assertIn("bool Hal::isAudioPlaying()", audio)


if __name__ == "__main__":
    unittest.main()
