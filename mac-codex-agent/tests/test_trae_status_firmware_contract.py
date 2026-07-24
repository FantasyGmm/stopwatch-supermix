import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class TraeStatusFirmwareContractTests(unittest.TestCase):
    def setUp(self):
        self.app_root = ROOT / "main" / "apps" / "app_codex_status"

    def test_watch_uses_a_clipped_two_page_strip_with_shared_footer(self):
        header = (self.app_root / "app_codex_status.h").read_text(encoding="utf-8")
        source = (self.app_root / "app_codex_status.cpp").read_text(
            encoding="utf-8"
        )

        for token in (
            "lv_obj_t* _page_strip",
            "lv_obj_t* _codex_page",
            "lv_obj_t* _trae_page_root",
            "lv_obj_t* _page_dot_codex",
            "lv_obj_t* _page_dot_trae",
            "std::unique_ptr<TraeStatusPage> _trae_status_page",
            "StatusPageSwipeLogic _page_swipe",
        ):
            self.assertIn(token, header)

        for token in (
            "lv_obj_set_size(_panel, 466, 466)",
            "lv_obj_set_size(_page_strip, 932, 466)",
            "lv_obj_set_pos(_codex_page, 0, 0)",
            "lv_obj_set_pos(_trae_page_root, 466, 0)",
            "lv_obj_align(_battery_label, LV_ALIGN_TOP_MID, 0, kBatteryY)",
            "lv_obj_set_y(dot, kPageIndicatorY)",
            "_page_dot_codex = make_page_dot(_panel, 223)",
            "_page_dot_trae = make_page_dot(_panel, 237)",
            "lv_anim_set_duration(&animation, 140)",
            "lv_anim_set_path_cb(&animation, lv_anim_path_ease_out)",
        ):
            self.assertIn(token, source)

        battery_create = source.index("_battery_label = lv_label_create(_panel)")
        strip_create = source.index("_page_strip = lv_obj_create(_panel)")
        self.assertGreater(battery_create, strip_create)

    def test_trae_page_keeps_model_and_effort_without_quota_widgets(self):
        header = (self.app_root / "trae_status_page.h").read_text(encoding="utf-8")
        source = (self.app_root / "trae_status_page.cpp").read_text(
            encoding="utf-8"
        )

        for token in (
            "class TraeStatusPage",
            "void init(lv_obj_t* parent)",
            "void render(",
            "void shutdown()",
            '"MODEL"',
            '"EFFORT"',
            '"STATE"',
            '"PROJECT"',
            '"TASK"',
            "make_signal_mark",
            "lv_obj_set_style_border_width",
            "LV_LABEL_LONG_MODE_DOTS",
        ):
            self.assertIn(token, header + source)

        for forbidden in (
            "lv_bar_create",
            "make_quota_row",
            "logo_trae",
            "lv_image_set_src",
        ):
            self.assertNotIn(forbidden, source)

    def test_trae_page_renders_all_visible_states_and_split_columns(self):
        source = (self.app_root / "trae_status_page.cpp").read_text(
            encoding="utf-8"
        )
        layout = (self.app_root / "trae_status_ui_layout.h").read_text(
            encoding="utf-8"
        )

        for text in (
            '"TRAE OFFLINE"',
            '"TRAE WAIT"',
            '"空闲"',
            '"等待指令"',
            '"正在处理"',
            '"已完成"',
        ):
            self.assertIn(text, source)

        for token in (
            "kLeftColumnX",
            "kLeftColumnWidth",
            "kRightColumnX",
            "kRightColumnWidth",
            "kColumnDividerX",
        ):
            self.assertIn(token, layout)

    def test_status_messages_route_to_independent_models_and_freshness(self):
        header = (self.app_root / "app_codex_status.h").read_text(encoding="utf-8")
        source = (self.app_root / "app_codex_status.cpp").read_text(
            encoding="utf-8"
        )

        for token in (
            "route_status_message(",
            "StatusMessageSource::Trae",
            "parse_trae_status_message",
            "TraeStatusSnapshot _trae_snapshot",
            "TraeTaskCarousel _trae_carousel",
            "_last_trae_status_payload_ms",
            "kTraeStatusStaleMs",
            "render_trae_status",
        ):
            self.assertIn(token, header + source)

        self.assertIn("_last_status_payload_ms", header)
        self.assertNotEqual(
            source.index("_last_status_payload_ms"),
            source.index("_last_trae_status_payload_ms"),
        )


if __name__ == "__main__":
    unittest.main()
