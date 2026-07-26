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
        self.assertIn("constexpr int kWeekY = 220", layout)
        self.assertIn("constexpr int kAlertDividerY = 258", layout)

        self.assertIn("constexpr int kPendingLabelX = 62", layout)
        self.assertIn("constexpr int kRunningLabelX = 308", layout)
        self.assertIn("constexpr int kPendingCountX = 78", layout)
        self.assertIn("constexpr int kRunningCountX = 324", layout)
        self.assertIn("constexpr int kStateLabelY = 278", layout)
        self.assertIn("constexpr int kStateCountY = 294", layout)
        self.assertIn("constexpr int kTaskIndexY = 284", layout)
        self.assertIn("constexpr int kAlertProjectY = 307", layout)
        self.assertIn("constexpr int kAlertDetailY = 359", layout)
        self.assertIn("constexpr int kAlertDetailHeight = 41", layout)
        self.assertIn("constexpr int kBatteryY = 432", layout)

        pending_block = app.split("_pending_label =", 1)[1].split(
            "_pending_count =", 1
        )[0]
        running_block = app.split("_running_label =", 1)[1].split(
            "_running_count =", 1
        )[0]
        self.assertIn("&MontserratBold16", pending_block)
        self.assertIn("LV_TEXT_ALIGN_CENTER", pending_block)
        self.assertIn("&MontserratBold16", running_block)
        self.assertIn("LV_TEXT_ALIGN_CENTER", running_block)

        pending_count_block = app.split("_pending_count =", 1)[1].split(
            "_running_label =", 1
        )[0]
        running_count_block = app.split("_running_count =", 1)[1].split(
            "_task_index =", 1
        )[0]
        self.assertIn("LV_TEXT_ALIGN_CENTER", pending_count_block)
        self.assertIn("LV_TEXT_ALIGN_CENTER", running_count_block)

    def test_state_words_use_bundled_true_bold_font(self):
        assets_header = (ROOT / "main/assets/assets.h").read_text()
        bold_font_path = ROOT / "main/assets/fonts/MontserratBold16.c"

        self.assertIn("LV_FONT_DECLARE(MontserratBold16);", assets_header)
        self.assertTrue(bold_font_path.is_file())
        bold_font = bold_font_path.read_text()
        self.assertIn("Size: 16 px", bold_font)
        self.assertIn("const lv_font_t MontserratBold16", bold_font)

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
        self.assertIn("LV_LABEL_LONG_MODE_WRAP", app)
        self.assertIn("lv_obj_set_height(_alert_detail, kAlertDetailHeight)", app)
        self.assertNotIn('state_label = "等待指令"', app)
        self.assertNotIn('state_label = "正在处理"', app)
        self.assertIn("lv_label_set_text(_alert_detail, alert->title.c_str())", app)

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
        self.assertIn("void set_state(CodexTaskState state, bool screen_off)", glow_header)
        self.assertIn("_pending_label", glow_header)
        self.assertIn("_pending_count", glow_header)
        self.assertIn("_running_label", glow_header)
        self.assertIn("_running_count", glow_header)
        self.assertIn("pulse_desired(state, screen_off)", glow_source)
        self.assertIn("ring_desired(state, screen_off)", glow_source)
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


if __name__ == "__main__":
    unittest.main()
