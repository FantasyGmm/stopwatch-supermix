import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = ROOT / "main/apps/app_codex_status/codex_task_focus_view.h"
SOURCE = ROOT / "main/apps/app_codex_status/codex_task_focus_view.cpp"


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
        self.assertIn("std::array<lv_obj_t*, 8> _track_accents", header)
        self.assertIn("lv_obj_t* _track_arc", header)
        self.assertIn("lv_arc_create(_track_layer)", source)
        self.assertIn("lv_arc_set_bg_angles(_track_arc", source)
        self.assertIn("shutdown();", source)
        self.assertEqual(source.count("lv_obj_create(_track_layer)"), 2)
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
        self.assertIn("lv_label_set_text(_detail_reply, reply)", source)
        self.assertNotIn("snprintf", source)
        self.assertNotIn("printf", source)


if __name__ == "__main__":
    unittest.main()
