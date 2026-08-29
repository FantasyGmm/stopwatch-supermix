import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class CodexStatusFLayoutContractTests(unittest.TestCase):
    def test_layout_matches_approved_single_quota_f_composition(self):
        layout = (
            ROOT / "main/apps/app_codex_status/codex_status_ui_layout.h"
        ).read_text()
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        header = (
            ROOT / "main/apps/app_codex_status/app_codex_status.h"
        ).read_text()

        self.assertNotIn("_five_hour_pct", header)
        self.assertNotIn("_five_hour_bar", header)
        self.assertNotIn('make_quota_row(_codex_page, kFiveHourY, "5H")', app)
        self.assertIn('make_quota_row(_codex_page, kWeekY, "7D")', app)
        self.assertIn("constexpr int kPetX = 55", layout)
        self.assertIn("constexpr int kPetY = 88", layout)
        self.assertIn("constexpr int kPetWidth = 144", layout)
        self.assertIn("constexpr int kPetHeight = 156", layout)
        self.assertIn("constexpr int kPetScale = 384", layout)
        self.assertIn("constexpr Rect kBeaconSpine{210, 96, 2, 120}", layout)
        self.assertIn("constexpr int kTelemetryX = 231", layout)
        self.assertIn("constexpr int kWeekY = 246", layout)
        self.assertIn("constexpr int kAlertDividerY = 282", layout)

        self.assertIn("constexpr int kPendingLabelX = 54", layout)
        self.assertIn("constexpr int kRunningLabelX = 300", layout)
        self.assertIn("constexpr int kPendingCountX = 78", layout)
        self.assertIn("constexpr int kRunningCountX = 324", layout)
        self.assertIn("constexpr int kStateLabelY = 294", layout)
        self.assertIn("constexpr int kStateLabelWidth = 112", layout)
        self.assertIn("constexpr int kStateCountY = 320", layout)
        self.assertIn("constexpr int kTaskIndexY = 306", layout)
        self.assertIn("constexpr int kAlertProjectY = 332", layout)
        self.assertIn("constexpr int kAlertDetailY = 372", layout)
        self.assertIn("constexpr int kAlertDetailHeight = 41", layout)
        self.assertIn("constexpr int kBatteryY = 432", layout)

        pending_block = app.split("_pending_label =", 1)[1].split(
            "_pending_count =", 1
        )[0]
        running_block = app.split("_running_label =", 1)[1].split(
            "_running_count =", 1
        )[0]
        self.assertIn("&MontserratBold22", pending_block)
        self.assertIn("LV_TEXT_ALIGN_CENTER", pending_block)
        self.assertIn("&MontserratBold22", running_block)
        self.assertIn("LV_TEXT_ALIGN_CENTER", running_block)

        pending_count_block = app.split("_pending_count =", 1)[1].split(
            "_running_label =", 1
        )[0]
        running_count_block = app.split("_running_count =", 1)[1].split(
            "_task_index =", 1
        )[0]
        self.assertIn("LV_TEXT_ALIGN_CENTER", pending_count_block)
        self.assertIn("LV_TEXT_ALIGN_CENTER", running_count_block)

    def test_state_words_use_bundled_22px_true_bold_font(self):
        assets_header = (ROOT / "main/assets/assets.h").read_text()
        bold_font_path = ROOT / "main/assets/fonts/MontserratBold22.c"

        self.assertIn("LV_FONT_DECLARE(MontserratBold22);", assets_header)
        self.assertTrue(bold_font_path.is_file())
        bold_font = bold_font_path.read_text()
        self.assertIn("Size: 22 px", bold_font)
        self.assertIn("const lv_font_t MontserratBold22", bold_font)

    def test_global_counts_and_centered_two_line_task_are_rendered_without_duplicate_state_copy(self):
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        header = (
            ROOT / "main/apps/app_codex_status/app_codex_status.h"
        ).read_text()

        for member in (
            "_pending_label",
            "_pending_count",
            "_running_label",
            "_running_count",
            "_task_index",
        ):
            self.assertIn(f"lv_obj_t* {member}", header)

        self.assertIn('"PENDING"', app)
        self.assertIn('"RUNNING"', app)
        self.assertIn("_alert_carousel.pending_count()", app)
        self.assertIn("_alert_carousel.running_count()", app)
        self.assertIn("LV_LABEL_LONG_MODE_DOTS", app)
        self.assertIn("lv_obj_set_size(_alert_info_row, kTaskInlineWidth, kAlertDetailHeight)", app)
        self.assertNotIn('state_label = "等待指令"', app)
        self.assertNotIn('state_label = "正在处理"', app)
        self.assertIn("lv_label_set_text(_alert_detail, alert->title.c_str())", app)
        self.assertIn("codex_task_model_display(alert->model.c_str())", app)
        self.assertIn("uppercase_short(effort, sizeof(effort), alert->effort.c_str())", app)

    def test_g3_task_metadata_centers_visible_content_and_matches_approved_type_scale(self):
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()

        self.assertIn(
            "lv_obj_set_style_max_width(_alert_detail, kTaskNameWidth, 0)",
            app,
        )
        self.assertIn("lv_obj_set_width(_alert_detail, LV_SIZE_CONTENT)", app)
        self.assertIn("TaskBadgeWidgets make_task_badge", app)
        self.assertEqual(app.count("make_task_badge(_alert_info_row,"), 1)
        self.assertIn("make_task_text(_alert_info_row, kTaskModelWidth)", app)
        task_text = app.split("lv_obj_t* make_task_text", 1)[1].split(
            "lv_obj_t* make_channel_dot", 1
        )[0]
        self.assertIn("&lv_font_montserrat_12", task_text)
        self.assertIn("lv_obj_set_width(label, width)", task_text)
        self.assertNotIn(
            "lv_obj_set_size(label, width, kTaskBadgeHeight)", task_text
        )
        self.assertIn(
            "lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0)",
            app,
        )
        self.assertIn("constexpr int kTaskTitleScale = 232", (
            ROOT / "main/apps/app_codex_status/codex_status_ui_layout.h"
        ).read_text())
        self.assertIn(
            "lv_obj_set_style_transform_scale_x(_alert_detail, kTaskTitleScale, 0)",
            app,
        )
        self.assertNotIn(
            "lv_obj_set_style_translate_y(_alert_detail", app
        )

    def test_voice_value_keeps_text_and_adds_a_colored_status_dot(self):
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        header = (
            ROOT / "main/apps/app_codex_status/app_codex_status.h"
        ).read_text()

        self.assertIn("_voice_status_dot", header)
        self.assertIn("_voice_status_dot = make_channel_dot(", app)
        self.assertIn("presentation.dot_color", app)
        self.assertIn("presentation.text", app)

    def test_agent_name_is_single_line_and_shrinks_before_eliding(self):
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()

        self.assertIn("fit_agent_name(measured.x, kTelemetryWidth)", app)
        self.assertIn("lv_obj_set_height(_agent_value, kAgentValueHeight)", app)
        self.assertIn("LV_LABEL_LONG_MODE_DOTS", app)

    def test_live_status_wins_over_stale_status_sampled_earlier_in_the_loop(self):
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        loop = app.split("void AppCodexStatus::onRunning()", 1)[1].split(
            "if (_transport_mode == \"auto\"", 1
        )[0]

        self.assertLess(
            loop.index("if (ble_connected && !_ble_was_connected)"),
            loop.index("if (ble_connected && _last_status_payload_ms != 0"),
        )
        self.assertLess(
            loop.index("if (ble_connected && _last_status_payload_ms != 0"),
            loop.index("while (_ble_transport->take_message(message))"),
        )

    def test_active_state_labels_share_the_glow_phase_without_an_underline(self):
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        glow_header = (
            ROOT / "main/apps/app_codex_status/codex_pending_soft_glow.h"
        ).read_text()
        glow_source = (
            ROOT / "main/apps/app_codex_status/codex_pending_soft_glow.cpp"
        ).read_text()

        self.assertIn("void set_status_targets(", glow_header)
        self.assertIn("bool surface_visible", glow_header)
        self.assertIn("_pending_label", glow_header)
        self.assertIn("_pending_count", glow_header)
        self.assertIn("_running_label", glow_header)
        self.assertIn("_running_count", glow_header)
        self.assertIn("state, screen_off, surface_visible", glow_source)
        self.assertIn("_pending_glow->set_status_targets(", app)
        self.assertIn("_pending_glow->set_state(", app)
        self.assertNotIn("active_mark", app.lower())

        phase_body = glow_source.split(
            "void CodexPendingSoftGlow::apply_phase", 1
        )[1].split("void CodexPendingSoftGlow::restore_target_opacity", 1)[0]
        self.assertNotIn("restore_target_opacity()", phase_body)
        self.assertIn("const bool state_changed = state != _state", glow_source)

    def test_standard_offline_gate_runs_all_codex_ui_native_tests(self):
        runner = (ROOT / "scripts/run_unified_offline_validation.sh").read_text()

        self.assertIn("tests/codex_pending_glow_logic_test.cpp", runner)
        self.assertIn("tests/codex_alert_carousel_test.cpp", runner)
        self.assertIn("tests/codex_status_ui_layout_test.cpp", runner)
        self.assertIn("tests/codex_pet_runtime_test.cpp", runner)

    def test_custom_pet_runtime_owns_psram_frames_and_only_observes_selection(self):
        runtime_header = (
            ROOT / "main/apps/app_codex_status/codex_pet_runtime.h"
        ).read_text()
        runtime_source = (
            ROOT / "main/apps/app_codex_status/codex_pet_runtime.cpp"
        ).read_text()
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()

        self.assertIn("class CodexPetRuntime", runtime_header)
        self.assertIn("bool open(const PetSelection& selection)", runtime_header)
        self.assertIn(
            "bool prepare_generation(const PetSelection& selection)", runtime_header
        )
        self.assertIn("void set_visible(bool visible, uint32_t now_ms)", runtime_header)
        self.assertIn("void advance(uint32_t now_ms)", runtime_header)
        self.assertIn("const lv_image_dsc_t* current_image() const", runtime_header)
        self.assertIn("bool take_frame_changed()", runtime_header)
        self.assertIn("void acknowledge_image_published()", runtime_header)
        self.assertIn("bool using_custom() const", runtime_header)
        self.assertIn("void close()", runtime_header)
        self.assertIn("LV_COLOR_FORMAT_RGB565A8", runtime_source)
        self.assertIn("kRgb565A8Bytes", runtime_source)
        self.assertIn("kSlotEnvelopeBytes", runtime_source)
        self.assertIn("decode_frame", runtime_source)
        self.assertIn("transitions < kMaxFrames", runtime_source)
        self.assertNotIn("CodexPetTransfer", runtime_header)
        self.assertIn("_pet_runtime->prepare_generation(_pet_selection)", app)
        self.assertIn("_pet_runtime->set_visible(", app)
        self.assertIn("_pet_runtime->close()", app)
        self.assertIn("lv_image_set_pivot(_pet_image, 0, 0);", app)
        self.assertIn("_pet_runtime->acknowledge_image_published();", app)
        apply_body = app.split("void AppCodexStatus::apply_pet_image()", 1)[1].split(
            "void AppCodexStatus::sync_pet_runtime", 1
        )[0]
        self.assertIn(
            "_pet_runtime->using_custom() ? LV_SCALE_NONE : kPetScale",
            apply_body,
        )
        self.assertIn("lv_obj_set_pos(_pet_image, kPetX, kPetY);", apply_body)
        self.assertLess(
            apply_body.index("lv_image_set_src(_pet_image, image);"),
            apply_body.index("_pet_runtime->acknowledge_image_published();"),
        )
        self.assertEqual(96 * 384 // 256, 144)
        self.assertEqual(104 * 384 // 256, 156)


if __name__ == "__main__":
    unittest.main()
