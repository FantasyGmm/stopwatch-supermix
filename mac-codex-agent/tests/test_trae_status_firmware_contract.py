import hashlib
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class TraeStatusFirmwareContractTests(unittest.TestCase):
    def setUp(self):
        self.app_root = ROOT / "main" / "apps" / "app_codex_status"

    def test_watch_uses_a_clipped_three_page_strip_with_shared_footer(self):
        header = (self.app_root / "app_codex_status.h").read_text(encoding="utf-8")
        source = (self.app_root / "app_codex_status.cpp").read_text(
            encoding="utf-8"
        )
        layout = (self.app_root / "codex_status_ui_layout.h").read_text(
            encoding="utf-8"
        )

        for token in (
            "lv_obj_t* _page_strip",
            "lv_obj_t* _codex_page",
            "lv_obj_t* _pinned_microphone_page",
            "lv_obj_t* _trae_page_root",
            "lv_obj_t* _page_dot_codex",
            "lv_obj_t* _page_dot_pinned_microphone",
            "lv_obj_t* _page_dot_trae",
            "std::unique_ptr<TraeStatusPage> _trae_status_page",
            "StatusPageSwipeLogic _page_swipe",
        ):
            self.assertIn(token, header)

        for token in (
            "lv_obj_set_size(_panel, 466, 466)",
            "lv_obj_set_size(_page_strip, 1398, 466)",
            "lv_obj_set_pos(_codex_page, 466, 0)",
            "lv_obj_set_pos(_pinned_microphone_page, 0, 0)",
            "lv_obj_set_pos(_trae_page_root, 932, 0)",
            "lv_obj_align(_battery_label, LV_ALIGN_TOP_MID, 0, kBatteryY)",
            "lv_obj_set_y(dot, kPageIndicatorY)",
            "_page_dot_pinned_microphone = make_page_dot(_panel, 213)",
            "_page_dot_codex = make_page_dot(_panel, 229)",
            "_page_dot_trae = make_page_dot(_panel, 245)",
            "lv_anim_set_duration(&animation, 140)",
            "lv_anim_set_path_cb(&animation, lv_anim_path_ease_out)",
        ):
            self.assertIn(token, source)

        self.assertIn("constexpr int kPageIndicatorY = 416;", layout)
        self.assertIn("constexpr int kBatteryY = 432;", layout)
        self.assertIn("int _current_page            = 1;", header)

        battery_create = source.index("_battery_label = lv_label_create(_panel)")
        strip_create = source.index("_page_strip = lv_obj_create(_panel)")
        self.assertGreater(battery_create, strip_create)

    def test_app_integrates_trae_task_touch_health_and_lifecycle(self):
        header = (self.app_root / "app_codex_status.h").read_text(encoding="utf-8")
        source = (self.app_root / "app_codex_status.cpp").read_text(
            encoding="utf-8"
        )

        for token in (
            "TraeCardTapLogic _trae_card_tap",
            "_trae_carousel.manual_next(now_ms)",
            "_trae_status_page->contains_task_point",
            "_trae_carousel.next()",
            "_trae_status_page->set_active(_current_page == 2 && !_screen_off)",
            "_ble_transport && _ble_transport->connected()",
        ):
            self.assertIn(token, header + source)

    def test_trae_page_keeps_model_and_effort_without_quota_widgets(self):
        header = (self.app_root / "trae_status_page.h").read_text(encoding="utf-8")
        source = (self.app_root / "trae_status_page.cpp").read_text(
            encoding="utf-8"
        )
        layout = (self.app_root / "trae_status_ui_layout.h").read_text(
            encoding="utf-8"
        )
        page_contract = header + source + layout

        for token in (
            "class TraeStatusPage",
            "void init(lv_obj_t* parent)",
            "void render(",
            "void set_active(bool active)",
            "bool contains_task_point(int x, int y) const",
            "void shutdown()",
            '"TRAE"',
            '"MODEL"',
            '"EFFORT"',
            '"BT LIVE"',
            '"TRAE LIVE"',
            '"待确认"',
            '"运行中"',
            "lv_obj_set_style_border_width",
            "LV_LABEL_LONG_MODE_DOTS",
        ):
            self.assertIn(token, page_contract)

        for forbidden in (
            "make_signal_mark",
            "kColumnDividerX",
            '"已完成"',
            "lv_bar_create",
            "make_quota_row",
            "logo_trae",
        ):
            self.assertNotIn(forbidden, source)

    def test_trae_page_renders_y_layout_and_empty_states(self):
        source = (self.app_root / "trae_status_page.cpp").read_text(
            encoding="utf-8"
        )
        layout = (self.app_root / "trae_status_ui_layout.h").read_text(
            encoding="utf-8"
        )

        for text in (
            '"暂无进行中的任务"',
            '"等待连接"',
            '"TRAE 未运行"',
            '"状态不可用"',
        ):
            self.assertIn(text, source + layout)

        for token in (
            "kBrandIconX",
            "kHealthX",
            "kRobotX",
            "kSummaryLabelY",
            "kSummaryCountY",
            "kCurrentProjectY",
            "kCurrentTaskY",
            "kNextTaskY",
        ):
            self.assertIn(token, layout)

    def test_trae_page_matches_approved_y_visual_contract(self):
        header = (self.app_root / "trae_status_page.h").read_text(encoding="utf-8")
        source = (self.app_root / "trae_status_page.cpp").read_text(
            encoding="utf-8"
        )
        layout = (self.app_root / "trae_status_ui_layout.h").read_text(
            encoding="utf-8"
        )
        page_contract = header + source + layout

        for token in (
            "lv_font_montserrat_10",
            "NotoSansSCBold16Trae",
            "kBluetoothDotX",
            "kProcessDotX",
            "kPendingAnchorX",
            "kRunningAnchorX",
            "kCurrentProjectX",
            "kCurrentTaskX",
            "kNextTaskX",
            '"下一任务"',
            "_next_task",
        ):
            self.assertIn(token, page_contract)

        for forbidden in (
            "make_card(",
            "_cards",
            "text_outline_stroke",
            '"PENDING"',
            '"RUNNING"',
            "kCardRadius",
            "kDeckHintY",
        ):
            self.assertNotIn(forbidden, page_contract)

        self.assertNotIn("_health_dot", page_contract)

    def test_trae_summary_uses_a_real_subset_cjk_bold_font(self):
        source = (self.app_root / "trae_status_page.cpp").read_text(
            encoding="utf-8"
        )
        layout = (self.app_root / "trae_status_ui_layout.h").read_text(
            encoding="utf-8"
        )
        font_path = ROOT / "main/assets/fonts/NotoSansSCBold16Trae.c"
        cmake = (ROOT / "main/CMakeLists.txt").read_text(encoding="utf-8")

        self.assertTrue(font_path.exists())
        font = font_path.read_text(encoding="utf-8")
        self.assertIn("NotoSansCJKsc-Bold.otf", font)
        self.assertIn("--symbols 待确认运行中", font)
        self.assertIn("assets/fonts/NotoSansSCBold16Trae.c", cmake)
        self.assertIn("LV_FONT_DECLARE(NotoSansSCBold16Trae)", source)
        self.assertIn("&NotoSansSCBold16Trae", source)
        self.assertNotIn("kSummaryLabelOutlineWidth", layout)
        self.assertNotIn("text_outline_stroke", source)

    def test_pinned_microphone_matches_locked_type_and_icon_geometry(self):
        header = (self.app_root / "app_codex_status.h").read_text(encoding="utf-8")
        source = (self.app_root / "app_codex_status.cpp").read_text(encoding="utf-8")
        cmake = (ROOT / "main/CMakeLists.txt").read_text(encoding="utf-8")

        for name in ("PinnedMicTitle24", "PinnedMicState22"):
            font_path = ROOT / f"main/assets/fonts/{name}.c"
            self.assertTrue(font_path.exists())
            self.assertIn(f"assets/fonts/{name}.c", cmake)
            self.assertIn(f"LV_FONT_DECLARE({name})", source)
            self.assertIn(f"&{name}", source)

        for token in (
            "_pinned_microphone_touch_target",
            "_pinned_microphone_connection_dot",
            "_pinned_microphone_connection_label",
            "_pinned_microphone_capsule",
            "_pinned_microphone_cup",
            "_pinned_microphone_stem",
            "_pinned_microphone_base",
            "_pinned_microphone_prompt_label",
            "lv_arc_create(_pinned_microphone_page)",
            "lv_obj_set_size(_pinned_microphone_touch_target, 194, 194)",
            "lv_obj_set_pos(_pinned_microphone_touch_target, 136, 126)",
            "lv_obj_set_size(_pinned_microphone_capsule, 36, 66)",
            "lv_obj_set_pos(_pinned_microphone_capsule, 215, 168)",
            "lv_obj_set_size(_pinned_microphone_cup, 62, 62)",
            "lv_obj_set_pos(_pinned_microphone_cup, 202, 190)",
            '"固定麦克风",\n        48,\n        &PinnedMicTitle24',
            '"已连接到 Mac",\n        84,\n        &font_puhui_14_1',
            '"轻触开启",\n        333,\n        &PinnedMicState22',
            "lv_obj_set_style_bg_opa(\n        _pinned_microphone_touch_target, LV_OPA_TRANSP, 0)",
            "lv_obj_set_style_arc_opa(_pinned_microphone_cup, LV_OPA_TRANSP, LV_PART_INDICATOR)",
            "_page_dot_pinned_microphone = make_page_dot(_panel, 213)",
            "_page_dot_codex = make_page_dot(_panel, 229)",
            "_page_dot_trae = make_page_dot(_panel, 245)",
        ):
            self.assertIn(token, header + source)
        self.assertNotIn("_pinned_microphone_beacon", header + source)
        self.assertNotIn("LV_SYMBOL_AUDIO", source)

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

    def test_approved_trae_robot_sources_keep_exact_provenance(self):
        expected = {
            "trae_robot_head.png": "063ef188f4d0df29a2708616bda5f84f5a0c38c47f6f60df590bc2c53d7422d9",
            "trae_robot_idle.png": "2927a2aab485b7b3b9f60f7189a6da132c2be28625a88c517204e345a0cf7c2e",
            "trae_robot_action.png": "5a6fbb08418469414c615b69389bd92d1817f3a2a5af0b35a1e910e927f7defa",
        }

        for name, digest in expected.items():
            source_path = self.app_root / "assets" / "source" / name
            self.assertTrue(source_path.is_file(), f"missing approved source: {name}")
            data = source_path.read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), digest)

    def test_trae_robot_assets_expose_three_rgb565a8_descriptors(self):
        header_path = self.app_root / "assets" / "trae_robot_assets.h"
        source_path = self.app_root / "assets" / "trae_robot_assets.cpp"
        self.assertTrue(header_path.is_file(), "missing generated asset header")
        self.assertTrue(source_path.is_file(), "missing generated asset source")
        header = header_path.read_text(encoding="utf-8")
        source = source_path.read_text(encoding="utf-8")
        names = (
            "trae_robot_head",
            "trae_robot_idle",
            "trae_robot_action",
        )

        for name in names:
            self.assertIn(f"extern const lv_image_dsc_t {name};", header)

        descriptors = re.findall(
            r"const lv_image_dsc_t ([a-z0-9_]+) = \{(.*?)\n\};",
            source,
            flags=re.DOTALL,
        )
        self.assertEqual([name for name, _ in descriptors], list(names))
        self.assertEqual(
            [
                body.count("LV_COLOR_FORMAT_RGB565A8")
                for _, body in descriptors
            ],
            [1, 1, 1],
        )


if __name__ == "__main__":
    unittest.main()
