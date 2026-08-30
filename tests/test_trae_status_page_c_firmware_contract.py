from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "main/apps/app_codex_status/trae_status_page.h"
SOURCE = ROOT / "main/apps/app_codex_status/trae_status_page.cpp"
APP_SOURCE = ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
PROTOTYPE = ROOT / "docs/prototypes/trae-status-layout.html"


class TraeStatusPageCContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = HEADER.read_text()
        cls.source = SOURCE.read_text()
        cls.app_source = APP_SOURCE.read_text()
        cls.prototype = PROTOTYPE.read_text()

    def test_robot_assets_and_timer_are_removed(self):
        combined = self.header + self.source
        self.assertNotIn("&trae_robot_idle", self.source)
        self.assertNotIn("&trae_robot_action", self.source)
        self.assertNotIn("robot_timer_cb", combined)
        self.assertNotIn("_robot_timer", combined)
        self.assertNotIn("_robot_action", combined)

    def test_page_owns_two_reusable_task_rows_with_effort_badges(self):
        self.assertIn("std::array<TaskRow, 2> _task_rows", self.header)
        self.assertIn("make_effort_badge", self.source)
        self.assertIn("render_task_row", self.source)

    def test_page_renders_agent_and_voice_channels(self):
        self.assertIn("const TraeChannelPresentation& channels", self.header)
        self.assertIn('"AGENT"', self.source)
        self.assertIn('"VOICE"', self.source)

    def test_app_passes_live_agent_and_voice_channels_to_trae_page(self):
        self.assertIn("TraeChannelPresentation presentation", self.app_source)
        self.assertIn("channels.connection.dot_color", self.app_source)
        self.assertIn("channels.voice.dot_color", self.app_source)
        self.assertIn("presentation,", self.app_source)

    def test_page_uses_scheme_c_spine_without_endpoint_dots(self):
        self.assertIn("make_spine", self.source)
        self.assertNotIn("make_spine_dot", self.source)

    def test_count_and_ascii_task_fonts_cover_the_rendered_characters(self):
        self.assertIn("&MontserratBold26Digits", self.source)
        self.assertIn("&MontserratBold24Trae", self.source)
        self.assertIn("&MontserratBold13Trae", self.source)
        self.assertIn("&MontserratBold17Trae", self.source)
        self.assertIn("&MontserratBold28Trae", self.source)
        self.assertIn("&MontserratMedium13Trae", self.source)
        self.assertIn("&MontserratMedium15Trae", self.source)
        self.assertGreaterEqual(self.source.count("&MontserratMedium15Trae"), 2)
        self.assertGreaterEqual(self.source.count("&MontserratBold15Trae"), 2)
        self.assertIn("&MontserratMedium14Trae", self.source)
        self.assertIn("&MontserratBold14Trae", self.source)
        self.assertNotIn("&MontserratBold22,", self.source)
        self.assertNotIn("&MontserratBold16,", self.source)
        self.assertNotIn("single ? LV_LABEL_LONG_MODE_WRAP", self.source)

    def test_single_task_metadata_flows_from_rendered_title_height(self):
        self.assertIn("single_task_meta_y(title_visual_height)", self.source)
        self.assertIn(
            "single ? single_task_meta_y(title_visual_height) : geometry.meta_y",
            self.source,
        )

    def test_task_group_uses_supported_bullet_separator(self):
        self.assertIn('"%s • %u"', self.source)
        self.assertNotIn('"%s · %u"', self.source)

    def test_scheme_c_prototype_records_the_second_one_pixel_increase(self):
        expected = (
            ".c .brand { position:absolute; top:26px; left:181px; font-size:29px; }",
            ".c .eyebrow { font-size:13px; }",
            ".c .rail-count strong { display:block; font-size:26px; line-height:1; }",
            ".c .c-task .title { margin-top:5px; font-size:24px;",
            "body.single-mode .c .primary-task .project { font-size:17px; }",
            "font-size:28px; line-height:1.13;",
            "body.single-mode .c .primary-task .meta { margin-top:13px; font-size:15px; gap:10px; }",
        )
        for token in expected:
            with self.subTest(token=token):
                self.assertIn(token, self.prototype)

    def test_scheme_c_prototype_records_status_and_summary_type_adjustment(self):
        expected = (
            ".c .rail-count span { position:relative; top:-3px; font-size:14px;",
            ".c .health-row .status-value { display:flex; align-items:center; gap:6px; font-size:15px;",
            "font-weight:700; min-width:0;",
            ".c .task-label { font-size:14px;",
            "body.single-mode .c .task-label { margin-top:18px; font-size:14px; }",
        )
        for token in expected:
            with self.subTest(token=token):
                self.assertIn(token, self.prototype)


if __name__ == "__main__":
    unittest.main()
