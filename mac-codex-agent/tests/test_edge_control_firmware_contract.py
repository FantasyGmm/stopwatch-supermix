import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class EdgeControlFirmwareContractTests(unittest.TestCase):
    def test_shared_overlay_is_presentation_only_and_centers_markers(self):
        edge_control_root = ROOT / "main" / "apps" / "common" / "edge_control"
        header = (edge_control_root / "edge_control_overlay.h").read_text(
            encoding="utf-8"
        )
        source = (edge_control_root / "edge_control_overlay.cpp").read_text(
            encoding="utf-8"
        )

        for token in (
            "class EdgeControlOverlay",
            "void init(lv_obj_t* parent)",
            "void show(EdgeControlSide side, int value)",
            "void hide()",
            "void shutdown()",
            "EdgeControlSide _visible_side",
            "int _current_value",
        ):
            self.assertIn(token, header)

        for token in (
            "LV_EVENT_DRAW_MAIN_BEGIN",
            "lv_obj_remove_event_cb_with_user_data",
            "draw_sun_icon",
            "draw_speaker_icon",
            "draw_percent_label",
            "_control_arc_radius - _control_arc_width / 2",
        ):
            self.assertIn(token, source)

        for forbidden in (
            "EdgeControlLogic _logic",
            "GetHAL()",
            "lv_indev_get_state",
            "_logic.press",
            "_logic.move",
            "_logic.release",
            "_logic.tick",
        ):
            self.assertNotIn(forbidden, header + source)

    def test_global_service_owns_touch_and_persists_hal_values(self):
        edge_control_root = ROOT / "main" / "apps" / "common" / "edge_control"
        header = (edge_control_root / "global_edge_control_service.h").read_text(
            encoding="utf-8"
        )
        source = (edge_control_root / "global_edge_control_service.cpp").read_text(
            encoding="utf-8"
        )

        for token in (
            "class GlobalEdgeControlService",
            "bool init()",
            "void update()",
            "bool ownsTouch() const",
            "bool lastReleaseConsumed() const",
            "uint32_t brightnessRevision() const",
            "uint32_t volumeRevision() const",
            "GlobalEdgeControlService& GetGlobalEdgeControlService()",
        ):
            self.assertIn(token, header)

        for token in (
            "lv_layer_top()",
            "lv_indev_wait_release(indev)",
            "lv_indev_reset(indev, nullptr)",
            "GetGlobalDisplayProtectionService().applyUserBrightness(",
            "GetHAL().setSpeakerVolume(update.value, update.persist)",
            "_brightness_revision++",
            "_volume_revision++",
        ):
            self.assertIn(token, source)
        self.assertNotIn("GetHAL().setBackLightBrightness", source)

    def test_display_protection_settings_uses_typed_checked_selector(self):
        source = (
            ROOT
            / "main"
            / "apps"
            / "app_setup"
            / "workers"
            / "display_protection.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("const lv_style_selector_t checked_indicator", source)
        self.assertIn(
            "static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |\n"
            "            static_cast<lv_style_selector_t>(LV_STATE_CHECKED)",
            source,
        )
        self.assertNotIn("LV_PART_INDICATOR | LV_STATE_CHECKED)", source)

    def test_global_service_updates_before_mooncake_apps(self):
        main = (ROOT / "main" / "main.cpp").read_text(encoding="utf-8")
        self.assertIn("GetGlobalEdgeControlService().init()", main)
        edge_update = main.index("GetGlobalEdgeControlService().update()")
        mooncake_update = main.index("GetMooncake().update()")
        self.assertLess(edge_update, mooncake_update)

    def test_launcher_uses_global_ownership_and_preserves_horizontal_swipes(self):
        launcher_root = ROOT / "main" / "apps" / "app_launcher" / "view"
        header = (launcher_root / "view.h").read_text(encoding="utf-8")
        source = (launcher_root / "view.cpp").read_text(encoding="utf-8")
        launcher = header + source

        self.assertNotIn("std::unique_ptr<EdgeControlOverlay>", launcher)
        for token in (
            "GetGlobalEdgeControlService()",
            "ownsTouch()",
            "lastReleaseConsumed()",
            "_edge_control_active",
            "_swipe_start_point",
            "_swipe_start_scroll_x",
            "_swipe_start_ms",
            "handle_horizontal_swipe_release",
        ):
            self.assertIn(token, launcher)

        for legacy in (
            "std::unique_ptr<uitk::lvgl_cpp::Container> _control_overlay",
            "_control_candidate",
            "_active_control",
            "activate_control(",
            "update_active_control(",
            "apply_control_value(",
            "launcher_control_overlay_draw_event_cb",
        ):
            self.assertNotIn(legacy, launcher)

    def test_codex_relies_on_global_overlay_and_keeps_boot_sound(self):
        codex_root = ROOT / "main" / "apps" / "app_codex_status"
        header = (codex_root / "app_codex_status.h").read_text(encoding="utf-8")
        source = (codex_root / "app_codex_status.cpp").read_text(encoding="utf-8")

        self.assertNotIn("EdgeControlOverlay", header + source)
        self.assertIn("GetHAL().playBootSfx()", source)
        self.assertNotIn("audio::play_tone(660", source)
        self.assertIn("GetGlobalEdgeControlService()", source)
        self.assertIn(
            "controls.ownsTouch() || controls.lastReleaseConsumed()", source
        )
        self.assertIn("_page_swipe.cancel()", source)
        self.assertIn("_last_page_touch_state = LV_INDEV_STATE_REL", source)

    def test_codex_scopes_task_deck_exclusion_to_its_lifecycle(self):
        service_header = (
            ROOT
            / "main"
            / "apps"
            / "common"
            / "edge_control"
            / "global_edge_control_service.h"
        ).read_text(encoding="utf-8")
        codex = (
            ROOT / "main" / "apps" / "app_codex_status" / "app_codex_status.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "void setTouchExclusion(std::optional<EdgeControlRect> exclusion)",
            service_header,
        )
        self.assertIn(
            "constexpr EdgeControlRect kTaskDeckTouchExclusion = {183, 423, 283, 465}",
            codex,
        )
        sync_exclusion = codex.index(
            "void AppCodexStatus::sync_task_edge_exclusion("
        )
        on_close = codex.index("void AppCodexStatus::onClose()")
        destroy_view = codex.index("void AppCodexStatus::destroy_view()")
        set_exclusion = codex.index(
            "std::optional<EdgeControlRect>{kTaskDeckTouchExclusion}",
            sync_exclusion,
        )
        task_mode_exclusion = codex.index(
            "std::optional<EdgeControlRect>{kTaskModeTouchExclusion}",
            set_exclusion,
        )
        clear_on_close = codex.index("setTouchExclusion(std::nullopt)", on_close)
        clear_on_destroy = codex.index(
            "setTouchExclusion(std::nullopt)", destroy_view
        )
        self.assertLess(sync_exclusion, set_exclusion)
        self.assertLess(set_exclusion, task_mode_exclusion)
        self.assertLess(on_close, clear_on_close)
        self.assertLess(destroy_view, clear_on_destroy)

    def test_watch_face_cancels_raw_gesture_when_global_touch_is_owned(self):
        source = (
            ROOT / "main" / "apps" / "app_watch_face" / "view" / "watch_face_manager.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("GetGlobalEdgeControlService()", source)
        self.assertIn(
            "controls.ownsTouch() || controls.lastReleaseConsumed()", source
        )
        self.assertIn("_gesture_pressing = false", source)

    def test_settings_workers_follow_global_brightness_and_volume_changes(self):
        workers_root = ROOT / "main" / "apps" / "app_setup" / "workers"
        header = (workers_root / "workers.h").read_text(encoding="utf-8")
        source = (workers_root / "device.cpp").read_text(encoding="utf-8")

        self.assertIn("void setCurrentValue(int value)", source)
        self.assertGreaterEqual(header.count("uint32_t _global_revision"), 2)
        for token in (
            "GetGlobalEdgeControlService()",
            "brightnessRevision()",
            "volumeRevision()",
            "GetHAL().getBackLightBrightness()",
            "GetHAL().getSpeakerVolume()",
            "_view->setCurrentValue",
            ):
            self.assertIn(token, source)

    def test_settings_disables_global_swipe_adjustment_only_while_open(self):
        service_header = (
            ROOT
            / "main"
            / "apps"
            / "common"
            / "edge_control"
            / "global_edge_control_service.h"
        ).read_text(encoding="utf-8")
        service_source = (
            ROOT
            / "main"
            / "apps"
            / "common"
            / "edge_control"
            / "global_edge_control_service.cpp"
        ).read_text(encoding="utf-8")
        settings = (
            ROOT / "main" / "apps" / "app_setup" / "app_setup.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("void setEnabled(bool enabled)", service_header)
        self.assertIn("if (!_initialized || !_enabled)", service_source)
        self.assertIn("GetGlobalEdgeControlService().setEnabled(false);", settings)
        self.assertIn("GetGlobalEdgeControlService().setEnabled(true);", settings)
        self.assertLess(
            settings.index("GetGlobalEdgeControlService().setEnabled(false);"),
            settings.index("_menu_page = std::make_unique<view::SelectMenuPage>"),
        )
        self.assertGreater(
            settings.index("GetGlobalEdgeControlService().setEnabled(true);"),
            settings.index("void AppSetup::onClose()"),
        )

    def test_global_volume_queues_assistant_volume_and_mute_sync(self):
        service = (
            ROOT
            / "main"
            / "apps"
            / "common"
            / "edge_control"
            / "global_edge_control_service.cpp"
        ).read_text(encoding="utf-8")
        supervisor = (ROOT / "main" / "runtime" / "runtime_supervisor.cpp").read_text(
            encoding="utf-8"
        )
        runtime = (
            ROOT / "main" / "runtime" / "assistant" / "assistant_runtime.cpp"
        ).read_text(encoding="utf-8")
        policy = (
            ROOT
            / "main"
            / "runtime"
            / "assistant"
            / "assistant_output_sync_policy.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("requestAssistantVolumeSync(update.value)", service)
        self.assertIn("requestOutputVolumeSync(volume)", supervisor)
        self.assertIn("AssistantRuntimeState::Starting", supervisor)
        self.assertIn("AssistantOutputSyncPolicy output_sync", runtime)
        self.assertIn("output_sync.request(volume)", runtime)
        self.assertIn("output_sync.begin(", runtime)
        self.assertIn("output_sync.markVolumeResult", runtime)
        self.assertIn("output_sync.markMuteResult", runtime)
        self.assertIn("SubmitAssistantOutputSyncPair", runtime)
        self.assertIn("submission.volume_threw", runtime)
        self.assertIn("submission.mute_threw", runtime)
        self.assertIn("result.success", runtime)
        self.assertIn("AudioHelper::call_function_async", runtime)
        self.assertIn("AudioHelper::FunctionId::SetVolume", runtime)
        self.assertIn("AudioHelper::FunctionId::SetMute", runtime)
        self.assertIn("volume <= 0", runtime)
        self.assertIn("CompletionTimeoutMs", policy)
        self.assertIn("RetryDelayMs", policy)
        self.assertIn("compare_exchange_strong", policy)

    def test_offline_runner_executes_edge_and_output_sync_host_tests(self):
        runner = (ROOT / "scripts" / "run_unified_offline_validation.sh").read_text(
            encoding="utf-8"
        )

        self.assertIn("Assistant output sync policy host test", runner)
        self.assertIn("tests/assistant_output_sync_policy_test.cpp", runner)
        self.assertIn(
            "main/runtime/assistant/assistant_output_sync_policy.cpp", runner
        )
        self.assertIn("Global edge control host test", runner)
        self.assertIn("tests/edge_control_logic_test.cpp", runner)
        self.assertIn(
            "main/apps/common/edge_control/edge_control_logic.cpp", runner
        )


if __name__ == "__main__":
    unittest.main()
