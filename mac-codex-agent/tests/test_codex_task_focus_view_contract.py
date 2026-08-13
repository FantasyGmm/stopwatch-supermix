import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = ROOT / "main/apps/app_codex_status/codex_task_focus_view.h"
SOURCE = ROOT / "main/apps/app_codex_status/codex_task_focus_view.cpp"
MONTSERRAT = ROOT / "main/assets/fonts/MontserratSemiBold26.c"
PUHUI = ROOT / "main/assets/fonts/font_puhui_14_1.c"


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
        self.assertIn("lv_obj_t* _track_arc", header)
        self.assertNotIn("_track_accents", header)
        self.assertIn("lv_arc_create(_track_layer)", source)
        self.assertIn("lv_arc_set_bg_angles(", source)
        self.assertIn("shutdown();", source)
        self.assertEqual(source.count("lv_obj_create(_track_layer)"), 1)
        self.assertNotIn("LV_GRAD_DIR", source)
        self.assertNotIn("shadow", source.lower())
        self.assertNotIn("glass", source.lower())
        self.assertNotIn("card", source.lower())

    def test_exact_copy_and_state_semantics_are_present(self):
        source = SOURCE.read_text(encoding="utf-8")

        for copy in (
            '"等待你的指令"',
            '"正在执行"',
            '"暂无进行中的任务"',
            '"暂无完整回复"',
            '"按住右键 · 发送下一步"',
            '"按住右键 · 引导当前任务"',
        ):
            self.assertIn(copy, source)
        self.assertIn("TaskTrackShape::PendingDiamond", source)
        self.assertIn("TaskTrackShape::RunningCircle", source)
        self.assertIn("TaskTrackShape::FocusedCapsule", source)
        self.assertIn("? kBeaconViolet", source)
        self.assertIn("lv_label_set_text(_detail_reply, reply)", source)
        self.assertIn("LV_LABEL_LONG_MODE_DOTS", source)
        self.assertIn("kDeckTitleCjkMaxScalars", source)
        self.assertIn("kDeckTitleLatinMaxScalars", source)
        self.assertIn("lv_obj_set_style_transform_scale_x", source)
        self.assertIn("kCjkTitleScale", source)
        self.assertIn("kCjkBodyScale", source)
        self.assertIn("kLatinTitleScale", source)
        self.assertNotIn("snprintf", source)
        self.assertNotIn("printf", source)

    def test_preview_feeds_the_real_rendering_api(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("void show_preview(", header)
        self.assertIn("TaskFocusPreviewScenario scenario", header)
        self.assertIn("task_focus_preview(scenario)", source)
        self.assertIn("show_deck(deck, data.connected)", source)
        self.assertIn("show_detail(&task, data.reply, data.connected)", source)
        self.assertIn("set_recording(data.recording)", source)
        self.assertIn("set_visible(true)", source)

    def test_font_sources_and_physical_scales_are_explicit(self):
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("Size: 26 px", MONTSERRAT.read_text(encoding="utf-8")[:200])
        self.assertIn("Size: 14 px", PUHUI.read_text(encoding="utf-8")[:200])
        self.assertIn("kLatinTitleScale = 315", (ROOT / "main/apps/app_codex_status/codex_task_focus_layout.h").read_text())
        self.assertIn("kCjkTitleScale = 476", (ROOT / "main/apps/app_codex_status/codex_task_focus_layout.h").read_text())
        self.assertIn("kCjkBodyScale = 293", (ROOT / "main/apps/app_codex_status/codex_task_focus_layout.h").read_text())


if __name__ == "__main__":
    unittest.main()
