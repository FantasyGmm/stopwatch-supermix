import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class EdgeControlFirmwareContractTests(unittest.TestCase):
    def test_shared_overlay_is_presentation_only_and_centers_markers(self):
        edge_control_root = ROOT / "main" / "apps" / "common" / "edge_control"
        header = (edge_control_root / "edge_control_overlay.h").read_text(
            encoding="utf-8"
        )
        source = (edge_control_root / "edge_control_overlay.cpp").read_text(
            encoding="utf-8"
        )

        for token in (
            "class EdgeControlOverlay",
            "void init(lv_obj_t* parent)",
            "void show(EdgeControlSide side, int value)",
            "void hide()",
            "void shutdown()",
            "EdgeControlSide _visible_side",
            "int _current_value",
        ):
            self.assertIn(token, header)

        for token in (
            "LV_EVENT_DRAW_MAIN_BEGIN",
            "lv_obj_remove_event_cb_with_user_data",
            "draw_sun_icon",
            "draw_speaker_icon",
            "draw_percent_label",
            "_control_arc_radius - _control_arc_width / 2",
        ):
            self.assertIn(token, source)

        for forbidden in (
            "EdgeControlLogic _logic",
            "GetHAL()",
            "lv_indev_get_state",
            "_logic.press",
            "_logic.move",
            "_logic.release",
            "_logic.tick",
        ):
            self.assertNotIn(forbidden, header + source)

    def test_launcher_uses_shared_overlay_and_preserves_horizontal_swipes(self):
        launcher_root = ROOT / "main" / "apps" / "app_launcher" / "view"
        header = (launcher_root / "view.h").read_text(encoding="utf-8")
        source = (launcher_root / "view.cpp").read_text(encoding="utf-8")
        launcher = header + source

        self.assertIn(
            "#include <apps/common/edge_control/edge_control_overlay.h>", header
        )
        self.assertIn("std::unique_ptr<EdgeControlOverlay> _edge_controls", header)
        for token in (
            "_swipe_start_point",
            "_swipe_start_scroll_x",
            "_swipe_start_ms",
            "handle_horizontal_swipe_release",
            "_edge_controls->update()",
            "_edge_controls->last_release_consumed()",
        ):
            self.assertIn(token, launcher)

        callback_start = source.index("_edge_controls->init")
        callback_end = source.index("});", callback_start)
        callback = source[callback_start:callback_end]
        self.assertEqual(
            callback.count(
                "_suppress_click_until_ms = GetHAL().millis() + 350;"
            ),
            2,
        )

        for legacy in (
            "std::unique_ptr<uitk::lvgl_cpp::Container> _control_overlay",
            "_control_candidate",
            "_active_control",
            "activate_control(",
            "update_active_control(",
            "apply_control_value(",
            "launcher_control_overlay_draw_event_cb",
        ):
            self.assertNotIn(legacy, launcher)

    def test_codex_uses_shared_overlay_and_full_boot_sound(self):
        codex_root = ROOT / "main" / "apps" / "app_codex_status"
        header = (codex_root / "app_codex_status.h").read_text(encoding="utf-8")
        source = (codex_root / "app_codex_status.cpp").read_text(encoding="utf-8")

        self.assertIn(
            "#include <apps/common/edge_control/edge_control_overlay.h>", header
        )
        self.assertIn("std::unique_ptr<EdgeControlOverlay> _edge_controls", header)
        for token in (
            "_edge_controls->init(_panel)",
            "_edge_controls->update()",
            "_edge_controls->shutdown()",
            "GetHAL().playBootSfx()",
        ):
            self.assertIn(token, source)
        self.assertNotIn("audio::play_tone(660", source)


if __name__ == "__main__":
    unittest.main()
