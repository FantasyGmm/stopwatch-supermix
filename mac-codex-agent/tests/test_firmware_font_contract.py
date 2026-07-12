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
            / "third_party"
            / "xiaozhi-esp32"
            / "managed_components"
            / "78__xiaozhi-fonts"
            / "src"
            / "font_puhui_14_1.c"
        ).read_text(encoding="utf-8")

        self.assertIn("font_puhui_14_1.c", cmake)
        self.assertIn("LV_FONT_DECLARE(font_puhui_14_1)", app)
        self.assertIn("lv_obj_set_style_text_font(_alert_project, &font_puhui_14_1", app)
        self.assertIn("lv_obj_set_style_text_font(_alert_detail, &font_puhui_14_1", app)
        for character in "中文项目进展监控":
            self.assertIn('U+{:04X} "{}"'.format(ord(character), character), font)

    def test_codex_alert_outcome_is_parsed_and_rendered_without_time(self):
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

        self.assertIn("bool waiting = false;", model_header)
        self.assertIn('value != "completed" && value != "waiting"', model_source)
        self.assertIn('value == "waiting"', model_source)
        self.assertIn("bool all_handled() const", carousel_header)
        self.assertIn('alert->waiting ? "等待指令" : "已完成"', app)
        self.assertNotIn('alert->time + " \\xC2\\xB7 "', app)

    def test_codex_alert_heading_uses_distinct_summary_colors(self):
        app = (self.codex_root / "app_codex_status.cpp").read_text(encoding="utf-8")
        carousel = (self.codex_root / "codex_alert_carousel.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("kHandledGreen", app)
        self.assertIn("_alert_carousel.all_handled()", app)
        self.assertIn("? kHandledGreen : kCodexRed", app)
        self.assertIn('return "COMPLETED";', carousel)
        self.assertNotIn("ALL HANDLED", carousel)
        self.assertNotIn('alert.pending ? "PENDING" : "HANDLED"', carousel)

    def test_model_and_effort_arcs_show_only_direct_values(self):
        app = (self.codex_root / "app_codex_status.cpp").read_text(encoding="utf-8")

        for legacy_caption in (
            'make_arc_value(_panel, 70, 138, "MODEL"',
            'make_arc_value(_panel, 306, 138, "EFFORT"',
            'make_arc_value(_panel, 70, 182, "auto"',
            'make_arc_value(_panel, 306, 182, "reason"',
        ):
            self.assertNotIn(legacy_caption, app)
        self.assertIn(
            '_model_value = make_arc_value(_panel, 70, 144, "--", '
            '&lv_font_montserrat_14',
            app,
        )
        self.assertIn(
            '_effort_value = make_arc_value(_panel, 306, 142, "--", '
            '&lv_font_montserrat_18',
            app,
        )
        self.assertIn("lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS)", app)


if __name__ == "__main__":
    unittest.main()
