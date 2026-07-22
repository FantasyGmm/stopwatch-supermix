import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class FirmwareFontContractTests(unittest.TestCase):
    def setUp(self):
        self.codex_root = ROOT / "main" / "apps" / "app_codex_status"

    def test_codex_alert_rows_use_the_bundled_chinese_font(self):
        cmake = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
        app = (
            ROOT / "main" / "apps" / "app_codex_status" / "app_codex_status.cpp"
        ).read_text(encoding="utf-8")
        font = (
            ROOT
            / "main"
            / "assets"
            / "fonts"
            / "font_puhui_14_1.c"
        ).read_text(encoding="utf-8")

        self.assertIn("font_puhui_14_1.c", cmake)
        self.assertIn("LV_FONT_DECLARE(font_puhui_14_1)", app)
        self.assertIn("lv_obj_set_style_text_font(_alert_project, &font_puhui_14_1", app)
        self.assertIn("lv_obj_set_style_text_font(_alert_detail, &font_puhui_14_1", app)
        for character in "中文项目进展监控":
            self.assertIn('U+{:04X} "{}"'.format(ord(character), character), font)

    def test_codex_alert_states_are_parsed_and_rendered_without_time(self):
        model_header = (self.codex_root / "codex_status_model.h").read_text(
            encoding="utf-8"
        )
        model_source = (self.codex_root / "codex_status_model.cpp").read_text(
            encoding="utf-8"
        )
        carousel_header = (self.codex_root / "codex_alert_carousel.h").read_text(
            encoding="utf-8"
        )
        app = (self.codex_root / "app_codex_status.cpp").read_text(encoding="utf-8")

        self.assertIn("enum class CodexTaskState", model_header)
        self.assertIn("CodexTaskState::Pending", model_source)
        self.assertIn("CodexTaskState::Running", model_source)
        self.assertIn("CodexTaskState::Completed", model_source)
        self.assertIn("CodexTaskState display_state() const", carousel_header)
        self.assertIn('"等待指令"', app)
        self.assertIn('"正在处理"', app)
        self.assertIn('"已完成"', app)
        self.assertNotIn('alert->time + " \\xC2\\xB7 "', app)

    def test_codex_alert_heading_uses_distinct_summary_colors(self):
        app = (self.codex_root / "app_codex_status.cpp").read_text(encoding="utf-8")
        carousel = (self.codex_root / "codex_alert_carousel.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("kCompletedGreen", app)
        self.assertIn("CodexTaskState::Running", app)
        self.assertIn("kCodexBlue", app)
        self.assertIn("kCompletedGreen", app)
        self.assertIn('return "COMPLETED";', carousel)
        self.assertIn('? "PENDING" : "RUNNING"', carousel)
        self.assertNotIn("ALL HANDLED", carousel)
        self.assertNotIn("all_handled", carousel)

    def test_model_and_effort_instruments_match_precision_console_mockup(self):
        app = (self.codex_root / "app_codex_status.cpp").read_text(encoding="utf-8")

        for legacy_caption in (
            'make_arc_value(_panel, 70, 138, "MODEL"',
            'make_arc_value(_panel, 306, 138, "EFFORT"',
            'make_arc_value(_panel, 70, 182, "auto"',
            'make_arc_value(_panel, 306, 182, "reason"',
        ):
            self.assertNotIn(legacy_caption, app)
        self.assertIn("make_instrument_frame(", app)
        self.assertNotIn("make_instrument_arc(", app)
        self.assertIn('132, "MODEL", &lv_font_montserrat_14, kEtchedGray', app)
        self.assertIn('132, "EFFORT", &lv_font_montserrat_14, kEtchedGray', app)
        self.assertIn("kModelInstrument, kAlertVermilion, true", app)
        self.assertIn("kEffortInstrument, kCalibrationBlue, false", app)
        self.assertIn("_model_value = make_instrument_value(", app)
        self.assertIn(
            '_panel, kModelInstrument, 157, "--", &lv_font_montserrat_18, kSignalWhite',
            app,
        )
        self.assertIn("_effort_value = make_instrument_value(", app)
        self.assertIn(
            '_panel, kEffortInstrument, 157, "--", &lv_font_montserrat_18, kSignalWhite',
            app,
        )
        self.assertIn('"Codex", kBrandX, kBrandY, &lv_font_montserrat_24, kSignalWhite', app)
        self.assertIn("make_line(_panel, kAlertDividerX, kAlertDividerY, kAlertDividerWidth)", app)
        self.assertIn("lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS)", app)


if __name__ == "__main__":
    unittest.main()
