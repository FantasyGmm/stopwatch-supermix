import hashlib
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class CodexStatusFirmwareContractTests(unittest.TestCase):
    def setUp(self):
        self.app_root = ROOT / "main" / "apps" / "app_codex_status"

    def test_watch_uses_a_clipped_four_page_strip_with_shared_footer(self):
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
            "lv_obj_t* _usage_page_root",
            "lv_obj_t* _host_page_root",
            "lv_obj_t* _page_dot_codex",
            "lv_obj_t* _page_dot_pinned_microphone",
            "lv_obj_t* _page_dot_usage",
            "lv_obj_t* _page_dot_host",
            "StatusPageSwipeLogic _page_swipe",
        ):
            self.assertIn(token, header)

        for token in (
            "lv_obj_set_size(_panel, 466, 466)",
            "lv_obj_set_size(_page_strip, kPageCount * kPageWidth, kPageWidth)",
            "lv_obj_set_pos(_page_strip, -466, 0)",
            "lv_obj_set_pos(_codex_page, 466, 0)",
            "lv_obj_set_pos(_pinned_microphone_page, 0, 0)",
            "_usage_page_root = make_status_page_root(_page_strip, 2 * kPageWidth)",
            "_host_page_root = make_status_page_root(_page_strip, 3 * kPageWidth)",
            "lv_obj_align(_battery_label, LV_ALIGN_TOP_MID, 0, kBatteryY)",
            "lv_obj_set_y(dot, kPageIndicatorY)",
            "_page_dot_pinned_microphone = make_page_dot(_panel, 205)",
            "_page_dot_codex = make_page_dot(_panel, 221)",
            "_page_dot_usage = make_page_dot(_panel, 237)",
            "_page_dot_host = make_page_dot(_panel, 253)",
            "lv_anim_set_duration(&animation, 140)",
            "lv_anim_set_path_cb(&animation, lv_anim_path_ease_out)",
        ):
            self.assertIn(token, source)

        self.assertRegex(source, r"constexpr int kPageCount\s*=\s*4;")
        self.assertIn("constexpr int kPageIndicatorY = 416;", layout)
        self.assertIn("constexpr int kBatteryY = 432;", layout)
        self.assertIn("int _current_page            = 1;", header)

        battery_create = source.index("_battery_label = lv_label_create(_panel)")
        strip_create = source.index("_page_strip = lv_obj_create(_panel)")
        self.assertGreater(battery_create, strip_create)

    def test_app_removes_trae_page_ui_and_preserves_lifecycle(self):
        header = (self.app_root / "app_codex_status.h").read_text(encoding="utf-8")
        source = (self.app_root / "app_codex_status.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            "_ble_transport && _ble_transport->connected()",
            header + source,
        )
        for token in (
            "TraeCardTapLogic",
            "_trae_status_page",
            "_trae_page_root",
            "_page_dot_trae",
            "_trae_carousel",
            "render_trae_status",
        ):
            self.assertNotIn(token, header + source)

    def test_usage_and_host_pages_render_required_offline_fields(self):
        source = (self.app_root / "codex_telemetry_pages.cpp").read_text(
            encoding="utf-8"
        )

        for text in (
            '"USAGE"',
            '"UNAVAILABLE"',
            '"5H LEFT"',
            '"7D LEFT"',
            '"--%"',
            '"RESET --"',
            '"ESTIMATED API EQUIVALENT • USD"',
            '"-- TOKENS"',
            '"HOST"',
            '"CPU"',
            '"MEM"',
            '"DISK"',
            '"NETWORK"',
            '"UP --"',
            '"DOWN --"',
        ):
            self.assertIn(text, source)
        self.assertEqual(source.count("render({}, 0);"), 2)

    def test_usage_and_host_layout_stays_above_footer(self):
        layout = (self.app_root / "codex_telemetry_ui_layout.h").read_text(
            encoding="utf-8"
        )

        for token in (
            "kUsageFiveHourY",
            "kUsageWeekY",
            "kUsageEstimateY",
            "kUsageTokensY",
            "kUsageProviderFirstY",
            "kUsageProviderSecondY",
            "kHostCpuY",
            "kHostMemoryY",
            "kHostDiskY",
            "kHostNetworkY",
            "kContentBottom",
            "kPageIndicatorY",
            "static_assert(kContentBottom < kPageIndicatorY)",
            "static_assert(kHostNetworkY + 43 < kContentBottom)",
        ):
            self.assertIn(token, layout)

    def test_telemetry_pages_are_initialized_from_the_snapshot(self):
        header = (self.app_root / "app_codex_status.h").read_text(encoding="utf-8")
        source = (self.app_root / "app_codex_status.cpp").read_text(
            encoding="utf-8"
        )

        for token in (
            "CodexUsagePage _usage_status_page",
            "CodexHostPage _host_status_page",
            "_usage_status_page.init(_usage_page_root)",
            "_host_status_page.init(_host_page_root)",
            "_usage_status_page.render(\n        _telemetry_snapshot.usage",
            "_host_status_page.render(\n        _telemetry_snapshot.host",
            "render_telemetry_pages();",
        ):
            self.assertIn(token, header + source)
        self.assertEqual(
            source.count("_usage_status_page.render(\n        _telemetry_snapshot.usage"),
            1,
        )
        self.assertEqual(
            source.count("_host_status_page.render(\n        _telemetry_snapshot.host"),
            1,
        )

    def test_telemetry_roots_keep_their_carousel_positions(self):
        app_source = (self.app_root / "app_codex_status.cpp").read_text(
            encoding="utf-8"
        )
        page_source = (self.app_root / "codex_telemetry_pages.cpp").read_text(
            encoding="utf-8"
        )

        for token in (
            "lv_obj_set_pos(page, x, 0)",
            "_usage_page_root = make_status_page_root(_page_strip, 2 * kPageWidth)",
            "_host_page_root = make_status_page_root(_page_strip, 3 * kPageWidth)",
            "_usage_status_page.init(_usage_page_root)",
            "_host_status_page.init(_host_page_root)",
        ):
            self.assertIn(token, app_source)
        self.assertNotIn("lv_obj_set_pos(parent, 0, 0)", page_source)

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

        state_font = (ROOT / "main/assets/fonts/PinnedMicState22.c").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "--symbols 轻触开启正在启动音频…收中·关闭等待 Mac 连接语异常重试暂不可用",
            state_font,
        )

        for token in (
            "_pinned_microphone_touch_target",
            "_pinned_microphone_connection_dot",
            "_pinned_microphone_connection_label",
            "_pinned_microphone_icon",
            "_pinned_microphone_level_left",
            "_pinned_microphone_level_right",
            "_pinned_microphone_prompt_label",
            "lv_obj_set_size(_pinned_microphone_touch_target, 232, 194)",
            "lv_obj_set_pos(_pinned_microphone_touch_target, 117, 122)",
            "lv_image_set_src(_pinned_microphone_icon, &pinned_microphone_d)",
            "lv_obj_set_pos(_pinned_microphone_icon, 185, 145)",
            '"固定麦克风",\n        48,\n        &PinnedMicTitle24',
            '"已连接到 Mac",\n        84,\n        &font_puhui_14_1',
            '"轻触开启",\n        333,\n        &PinnedMicState22',
            "lv_obj_set_style_bg_opa(\n        _pinned_microphone_touch_target, LV_OPA_TRANSP, 0)",
            "_page_dot_pinned_microphone = make_page_dot(_panel, 205)",
            "_page_dot_codex = make_page_dot(_panel, 221)",
            "_page_dot_usage = make_page_dot(_panel, 237)",
            "_page_dot_host = make_page_dot(_panel, 253)",
        ):
            self.assertIn(token, header + source)
        self.assertNotIn("_pinned_microphone_beacon", header + source)
        self.assertNotIn("_pinned_microphone_internal_meter", header + source)
        self.assertNotIn("_pinned_microphone_capsule", header + source)
        self.assertNotIn("_pinned_microphone_cup", header + source)
        self.assertIn("LV_OBJ_FLAG_HIDDEN", source)
        self.assertNotIn("LV_SYMBOL_AUDIO", source)

        asset = (
            ROOT
            / "main/apps/app_codex_status/assets/pinned_microphone_d.c"
        ).read_text(encoding="utf-8")
        self.assertIn("LV_COLOR_FORMAT_A8", asset)
        self.assertIn(".header.w = 96", asset)
        self.assertIn(".header.h = 138", asset)

    def test_status_messages_keep_trae_parser_compatibility_without_a_page_model(self):
        header = (self.app_root / "app_codex_status.h").read_text(encoding="utf-8")
        source = (self.app_root / "app_codex_status.cpp").read_text(
            encoding="utf-8"
        )

        for token in (
            "route_status_message(",
            "StatusMessageSource::Trae",
            "parse_trae_status_message",
            "TraeStatusSnapshot snapshot",
        ):
            self.assertIn(token, header + source)

        self.assertNotIn("TraeStatusSnapshot _trae_snapshot", header + source)
        self.assertNotIn("render_trae_status", header + source)
        self.assertIn("_last_status_payload_ms", header)

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
