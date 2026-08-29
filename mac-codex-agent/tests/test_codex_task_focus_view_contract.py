import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = ROOT / "main/apps/app_codex_status/codex_task_focus_view.h"
SOURCE = ROOT / "main/apps/app_codex_status/codex_task_focus_view.cpp"
PUHUI = ROOT / "main/assets/fonts/font_puhui_14_1.c"
DETAIL_FONT = ROOT / "main/assets/fonts/CodexDetail17.c"
FOCUSED_TITLE_FONT = ROOT / "main/assets/fonts/CodexTaskTitle21.c"
SDKCONFIG_DEFAULTS = ROOT / "sdkconfig.defaults"


class CodexTaskFocusViewContractTests(unittest.TestCase):
    def test_reusable_view_api_and_fixed_object_tree(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        for signature in (
            "void init(lv_obj_t* parent)",
            "void shutdown()",
            "void show_deck(const CodexTaskFocusDeck& deck, bool connected)",
            "void show_detail(",
            "void set_recording(bool recording)",
            "void set_visible(bool visible)",
            "bool contains_focus_point(int x, int y) const",
            "void detail_scroll_by(int delta_y)",
            "bool detail_at_top() const",
        ):
            self.assertIn(signature, header)

        self.assertIn("std::array<lv_obj_t*, 8> _track_nodes", header)
        self.assertIn("std::array<lv_obj_t*, 16> _detail_paragraphs", header)
        self.assertIn("lv_obj_t* _detail_drag_handle", header)
        self.assertNotIn("_detail_live_dot", header)
        self.assertNotIn("_track_arc", header)
        self.assertNotIn("_track_accents", header)
        self.assertNotIn("_detail_back", header)
        self.assertNotIn("lv_arc_create(_track_layer)", source)
        self.assertNotIn("lv_arc_set_bg_angles(", source)
        self.assertNotIn('"‹ 返回任务"', source)
        self.assertIn("shutdown();", source)
        self.assertEqual(source.count("lv_obj_create(_track_layer)"), 1)
        self.assertIn("make_fading_separator(", source)
        self.assertIn("LV_GRAD_DIR_HOR", source)
        self.assertIn("kStreamSeparatorFadeWidth", source)
        self.assertIn("kStreamSeparatorCenterWidth", source)
        self.assertNotIn("shadow", source.lower())
        self.assertNotIn("glass", source.lower())
        self.assertNotIn("card", source.lower())

    def test_exact_copy_and_state_semantics_are_present(self):
        source = SOURCE.read_text(encoding="utf-8")
        layout = (
            ROOT / "main/apps/app_codex_status/codex_task_focus_layout.h"
        ).read_text(encoding="utf-8")

        for copy in (
            '"等待你的指令"',
            '"正在执行"',
            '"暂无进行中的任务"',
            '"暂无完整回复"',
            '"当前语音目标"',
            '"上下滑动切换　↓"',
            '"Agent 最后一条完整回复"',
            '"按住右键 · 发送下一步"',
            '"按住右键 · 引导当前任务"',
        ):
            self.assertIn(copy, source)
        self.assertNotIn("TaskTrackShape::PendingDiamond", source)
        self.assertNotIn('"Agent 实时进展"', source)
        self.assertIn("kDetailDragHandleX", source)
        self.assertIn("kDetailDragHandleY", source)
        self.assertIn("kDetailDragHandleWidth", source)
        self.assertIn("kDetailDragHandleHeight", source)
        self.assertIn("kDetailHandleOpacity", source)
        self.assertIn("TaskTrackShape::RunningCircle", layout)
        self.assertIn("TaskTrackShape::FocusedCapsule", layout)
        self.assertIn("kTaskTrackFocused", layout)
        self.assertIn("kTaskTrackInactive", layout)
        self.assertIn("split_detail_reply(", source)
        self.assertIn("detail_display_text(reply)", source)
        self.assertIn("if (reset_scroll)", source)
        self.assertIn("_detail_paragraph_text[index].c_str()", source)
        self.assertIn("kDetailReplyLineHeight", source)
        self.assertIn("kDetailParagraphGap", source)
        self.assertIn("LV_LABEL_LONG_MODE_DOTS", source)
        self.assertIn("lv_obj_set_style_transform_scale_x", source)
        self.assertIn("kStreamFocusedCjkScale", source)
        self.assertIn("detail_body_font()", source)
        self.assertNotIn("set_text_scale(paragraph", source)
        self.assertNotIn("snprintf", source)
        self.assertNotIn("printf", source)

    def test_preview_feeds_the_real_rendering_api(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("void show_preview(", header)
        self.assertIn("TaskFocusPreviewScenario scenario", header)
        self.assertIn("task_focus_preview(scenario)", source)
        self.assertIn("show_deck(deck, data.connected)", source)
        self.assertIn("show_detail(&task, data.reply, 0, data.connected)", source)
        self.assertIn("set_recording(data.recording)", source)
        self.assertIn("set_visible(true)", source)

    def test_font_sources_and_physical_scales_are_explicit(self):
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("Size: 14 px", PUHUI.read_text(encoding="utf-8")[:200])
        self.assertIn(
            "Size: 21 px",
            FOCUSED_TITLE_FONT.read_text(encoding="utf-8")[:200],
        )
        layout = (ROOT / "main/apps/app_codex_status/codex_task_focus_layout.h").read_text()
        self.assertIn("kStreamFocusedCjkScale = 256", layout)
        self.assertIn("kStreamNeighborCjkScale = 256", layout)
        self.assertIn("kDetailBodyFontPx = 17", layout)
        self.assertIn("kDetailReplyLineHeight = 27", layout)
        self.assertIn("LV_FONT_DECLARE(CodexTaskTitle21)", source)
        self.assertIn(
            "cjk ? &CodexTaskTitle21 : &MontserratSemiBold26",
            source,
        )
        for size in (9, 10, 11, 16):
            static_font = ROOT / f"main/assets/fonts/CodexFocusStatic{size}.c"
            self.assertTrue(static_font.is_file())
            self.assertIn(f"Size: {size} px", static_font.read_text()[:200])

    def test_fixed_detail_fonts_cover_every_rendered_label(self):
        def generated_symbols(size):
            source = (
                ROOT / f"main/assets/fonts/CodexFocusStatic{size}.c"
            ).read_text(encoding="utf-8")[:2000]
            return source.split("--symbols ", 1)[1].split(" --no-kerning", 1)[0]

        required = {
            10: "当前语音目标正在录音",
            11: "等待你的指令正在执行待回复运行中Agent 最后一条完整回复",
            16: (
                "任务上下滑动切换　↓按住右键 · 发送下一步引导当前任务"
                "连接已断开语音不可用松开右键发送"
            ),
        }
        for size, labels in required.items():
            symbols = set(generated_symbols(size))
            self.assertEqual(set(labels) - symbols, set(), f"missing {size}px glyphs")

    def test_detail_reply_uses_one_native_font_without_fallback_or_scaling(self):
        source = SOURCE.read_text(encoding="utf-8")
        detail_font = DETAIL_FONT.read_text(encoding="utf-8")
        body_font = source.split(
            "const lv_font_t* detail_body_font()", 1
        )[1].split("void set_text_scale", 1)[0]

        self.assertIn("LV_FONT_DECLARE(CodexDetail17)", source)
        self.assertIn("return &CodexDetail17;", body_font)
        self.assertNotIn("fallback", body_font)
        self.assertNotIn("font_puhui_14_1", body_font)
        self.assertIn("Size: 17 px", detail_font[:200])
        self.assertIn("Bpp: 2", detail_font[:200])
        self.assertIn(".fallback = NULL", detail_font)
        self.assertIn(".bitmap_format = 2", detail_font)
        self.assertIn(
            "CONFIG_LV_USE_FONT_COMPRESSED=y",
            SDKCONFIG_DEFAULTS.read_text(encoding="utf-8"),
        )
        self.assertNotIn("set_text_scale(paragraph", source)


if __name__ == "__main__":
    unittest.main()
