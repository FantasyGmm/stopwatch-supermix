import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class FirmwareFontContractTests(unittest.TestCase):
    def setUp(self):
        self.codex_root = ROOT / "main" / "apps" / "app_codex_status"

    def test_codex_alert_rows_use_project_font_fallback_for_non_ascii_text(self):
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
        self.assertIn("bool is_ascii_text(const char* text)", app)
        self.assertIn("is_ascii_text(alert->project.c_str())", app)
        self.assertIn("? &lv_font_montserrat_22", app)
        self.assertIn(": &font_puhui_14_1", app)
        self.assertIn("lv_obj_set_style_text_font(_alert_detail, &font_puhui_14_1", app)
        self.assertIn("kSecondaryText", app)
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
        app = (self.codex_root / "app_codex_status.cpp").read_text(
            encoding="utf-8"
        )
        layout = (self.codex_root / "codex_status_ui_layout.h").read_text(
            encoding="utf-8"
        )
        carousel = (self.codex_root / "codex_alert_carousel.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            "constexpr uint32_t kRunningCyan = 0x49C9EE", layout
        )
        self.assertIn("CodexTaskState::Running", app)
        self.assertIn("state_color = kRunningCyan;", app)
        self.assertIn(
            '_panel, "--", kAlertProjectY, &lv_font_montserrat_22, '
            "kBeaconViolet",
            app,
        )
        self.assertIn("kCompletedGreen", app)
        self.assertIn('return "COMPLETED";', carousel)
        self.assertIn('? "PENDING" : "RUNNING"', carousel)
        self.assertNotIn("ALL HANDLED", carousel)
        self.assertNotIn("all_handled", carousel)

    def test_codex_status_matches_beacon_spine_mockup_and_reuses_assets(self):
        app = (self.codex_root / "app_codex_status.cpp").read_text(encoding="utf-8")
        layout = (self.codex_root / "codex_status_ui_layout.h").read_text(
            encoding="utf-8"
        )

        for legacy_caption in (
            'make_arc_value(_panel, 70, 138, "MODEL"',
            'make_arc_value(_panel, 306, 138, "EFFORT"',
            'make_arc_value(_panel, 70, 182, "auto"',
            'make_arc_value(_panel, 306, 182, "reason"',
        ):
            self.assertNotIn(legacy_caption, app)
        self.assertNotIn("make_instrument_frame(", app)
        self.assertNotIn("make_instrument_arc(", app)
        self.assertNotIn("make_outline(", app)
        self.assertNotIn("kPetDock", layout)
        self.assertNotIn('make_center_label(_panel, "", kHostY', app)
        self.assertNotIn("kHostY", layout)
        self.assertIn("kBeaconSpine.x", app)
        self.assertIn("kBeaconSpine.y", app)
        self.assertIn("kBeaconViolet", app)
        self.assertIn("_model_value = make_telemetry_label(", app)
        self.assertIn("kModelCaptionY", app)
        self.assertIn("kModelValueY", app)
        self.assertIn("_effort_value = make_telemetry_label(", app)
        self.assertIn("kEffortCaptionY", app)
        self.assertIn("kEffortValueY", app)
        self.assertGreaterEqual(app.count("&MontserratSemiBold26"), 3)
        self.assertIn(
            '_model_value = make_telemetry_label(\n'
            '        _panel,\n'
            '        kTelemetryX,\n'
            '        kModelValueY,\n'
            '        kTelemetryWidth,\n'
            '        "--",\n'
            '        &MontserratSemiBold26,',
            app,
        )
        self.assertIn(
            '_effort_value = make_telemetry_label(\n'
            '        _panel,\n'
            '        kTelemetryX,\n'
            '        kEffortValueY,\n'
            '        kTelemetryWidth,\n'
            '        "--",\n'
            '        &MontserratSemiBold26,',
            app,
        )
        self.assertIn(
            "lv_obj_set_style_text_font(pct, &lv_font_montserrat_22, 0)", app
        )
        self.assertIn(
            "lv_obj_set_style_text_letter_space(pct, 0, 0);", app
        )
        self.assertIn(
            '_panel, "NO ALERTS", kAlertHeadingY, &lv_font_montserrat_20, kEtchedGray',
            app,
        )
        self.assertIn(
            '_panel, "--", kAlertProjectY, &lv_font_montserrat_22, kBeaconViolet',
            app,
        )
        self.assertIn(
            "lv_obj_set_style_text_font(_alert_project, &lv_font_montserrat_22, 0)",
            app,
        )
        self.assertIn("? &lv_font_montserrat_22", app)
        self.assertIn("lv_color_hex(kQuotaMint)", app)
        self.assertIn("constexpr uint32_t kInstrumentBlack = 0x000000", layout)
        self.assertIn("constexpr Rect kBeaconSpine{194, 96, 2, 104}", layout)
        self.assertIn("constexpr int kTelemetryX = 214", layout)
        self.assertIn("constexpr int kTelemetryWidth = 200", layout)
        self.assertIn("constexpr int kQuotaBarX = 101", layout)
        self.assertIn("constexpr int kQuotaBarWidth = 248", layout)
        self.assertIn("constexpr int kQuotaPercentX = 351", layout)
        self.assertIn("constexpr int kQuotaPercentWidth = 60", layout)
        self.assertIn("lv_image_set_src(logo, &logo_codex)", app)
        self.assertIn("lv_image_set_scale(logo, kLogoScale)", app)
        self.assertIn("lv_image_set_src(_pet_image, codex_pet_idle_frames[0])", app)
        self.assertIn("lv_image_set_antialias(_pet_image, false)", app)
        self.assertIn("lv_image_set_scale(_pet_image, kPetScale)", app)
        self.assertIn("lv_obj_set_pos(_pet_image, kPetX, kPetY)", app)
        self.assertIn("lv_obj_set_pos(pct, kQuotaPercentX, y - 5)", app)
        self.assertIn(
            "lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_RIGHT, 0)", app
        )
        self.assertIn("codex_pet_idle_durations_ms[self->_pet_frame]", app)
        self.assertIn('"Codex", kBrandX, kBrandY, &MontserratSemiBold26, kSignalWhite', app)
        self.assertIn("make_line(_panel, kAlertDividerX, kAlertDividerY, kAlertDividerWidth)", app)
        self.assertIn("lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS)", app)

    def test_quota_percentages_fit_bundled_22px_font_without_compression(self):
        app = (self.codex_root / "app_codex_status.cpp").read_text(encoding="utf-8")
        quota_row = app.split("QuotaWidgets make_quota_row", 1)[1].split(
            "lv_obj_t* make_line", 1
        )[0]

        letter_space_px = 0
        box_width_px = 60
        self.assertIn(
            "lv_obj_set_style_text_letter_space(pct, 0, 0);", quota_row
        )
        self.assertIn(
            "lv_label_set_long_mode(pct, LV_LABEL_LONG_MODE_CLIP);", quota_row
        )

        font = (
            ROOT
            / "components"
            / "lvgl"
            / "src"
            / "font"
            / "lv_font_montserrat_22.c"
        ).read_text(encoding="utf-8")
        glyph_block = re.search(
            r"static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc\[\] = \{"
            r"(?P<body>.*?)\n\};",
            font,
            re.DOTALL,
        )
        self.assertIsNotNone(glyph_block)
        advances_16 = [
            int(value)
            for value in re.findall(r"\.adv_w = (\d+)", glyph_block.group("body"))
        ]

        cmap = re.search(
            r"\.range_start\s*=\s*(?P<range_start>\d+),.*?"
            r"\.range_length\s*=\s*(?P<range_length>\d+),.*?"
            r"\.glyph_id_start\s*=\s*(?P<glyph_id_start>\d+),",
            font,
            re.DOTALL,
        )
        self.assertIsNotNone(cmap)
        range_start = int(cmap.group("range_start"))
        range_length = int(cmap.group("range_length"))
        glyph_id_start = int(cmap.group("glyph_id_start"))
        self.assertEqual(range_start, 0x20)

        expected_metrics = {
            "--%": ([135, 135, 297], 567, 35),
            "99%": ([217, 217, 297], 731, 47),
            "100%": ([130, 235, 235, 297], 897, 57),
        }

        def measure_text(text):
            glyph_advances = []
            for character in text:
                codepoint = ord(character)
                self.assertLess(codepoint, range_start + range_length)
                glyph_id = glyph_id_start + codepoint - range_start
                glyph_advances.append(advances_16[glyph_id])

            width_16 = sum(glyph_advances) + (
                letter_space_px * 16 * (len(text) - 1)
            )
            lvgl_width_px = sum(
                (advance_16 + 8) >> 4 for advance_16 in glyph_advances
            ) + letter_space_px * (len(text) - 1)
            return glyph_advances, width_16, lvgl_width_px

        for text, expected in expected_metrics.items():
            with self.subTest(text=text):
                glyph_advances, width_16, lvgl_width_px = measure_text(text)
                self.assertEqual(
                    (glyph_advances, width_16, lvgl_width_px), expected
                )
                self.assertLessEqual(width_16, box_width_px * 16)
                self.assertLessEqual(lvgl_width_px, box_width_px)

        for value in range(101):
            text = f"{value}%"
            with self.subTest(text=text):
                _, width_16, lvgl_width_px = measure_text(text)
                self.assertLessEqual(width_16, box_width_px * 16)
                self.assertLessEqual(lvgl_width_px, box_width_px)


if __name__ == "__main__":
    unittest.main()
