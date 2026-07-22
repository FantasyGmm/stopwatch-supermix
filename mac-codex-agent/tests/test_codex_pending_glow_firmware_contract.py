import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class CodexPendingGlowFirmwareContractTests(unittest.TestCase):
    def test_soft_glow_component_uses_three_fixed_rings_and_one_opacity_animation(self):
        header = (
            ROOT / "main/apps/app_codex_status/codex_pending_soft_glow.h"
        ).read_text()
        source = (
            ROOT / "main/apps/app_codex_status/codex_pending_soft_glow.cpp"
        ).read_text()

        self.assertIn("std::array<lv_obj_t*, 3> _rings", header)
        self.assertIn("void set_heading(lv_obj_t* label)", header)
        self.assertIn("void set_active(bool active)", header)
        self.assertIn("void shutdown()", header)
        self.assertIn("_rings[0] = make_ring(parent, 1, 1, 464, 2)", source)
        self.assertIn("_rings[1] = make_ring(parent, 5, 5, 456, 4)", source)
        self.assertIn("_rings[2] = make_ring(parent, 11, 11, 444, 6)", source)
        self.assertIn("lv_anim_set_duration(&animation, 900)", source)
        self.assertIn("lv_anim_set_playback_duration(&animation, 900)", source)
        self.assertIn(
            "lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE)", source
        )
        self.assertIn(
            "lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out)", source
        )
        self.assertIn("lv_obj_set_style_border_opa", source)
        phase_body = source.split(
            "void CodexPendingSoftGlow::apply_phase", 1
        )[1]
        phase_body = phase_body.split("void CodexPendingSoftGlow::start", 1)[0]
        self.assertNotIn("lv_obj_set_size", phase_body)
        self.assertNotIn("lv_obj_set_pos", phase_body)

    def test_component_stops_animation_and_restores_heading_on_deactivation(self):
        source = (
            ROOT / "main/apps/app_codex_status/codex_pending_soft_glow.cpp"
        ).read_text()

        self.assertIn("lv_anim_delete(this, animation_cb)", source)
        self.assertIn("lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN)", source)
        self.assertIn("lv_obj_set_style_opa(_heading, LV_OPA_COVER, 0)", source)

    def test_latest_codex_page_owns_effect_below_content_and_preserves_geometry(self):
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        header = (
            ROOT / "main/apps/app_codex_status/app_codex_status.h"
        ).read_text()
        layout = (
            ROOT / "main/apps/app_codex_status/codex_status_ui_layout.h"
        ).read_text()

        self.assertIn(
            "std::unique_ptr<CodexPendingSoftGlow> _pending_glow", header
        )
        self.assertIn("void sync_pending_glow()", header)
        self.assertLess(
            app.index("_pending_glow->init(_panel)"),
            app.index("lv_image_create(_panel)"),
        )
        self.assertLess(
            app.index("_pending_glow->set_heading(_activity_title)"),
            app.index("_battery_label = lv_label_create(_panel)"),
        )
        self.assertIn("codex_pending_glow::desired_active(", app)
        self.assertIn("_alert_carousel.display_state()", app)
        self.assertIn("_pending_glow->set_active(active)", app)
        self.assertIn("_pending_glow->shutdown()", app)
        self.assertIn("constexpr uint32_t kInstrumentBlack = 0x000000", layout)
        self.assertIn("constexpr int kPetX = 73", layout)
        self.assertIn("constexpr int kPetY = 88", layout)
        self.assertIn("constexpr int kPetScale = 293", layout)
        self.assertIn("constexpr Rect kBeaconSpine{194, 96, 2, 104}", layout)
        self.assertNotIn("kHostY", layout)


if __name__ == "__main__":
    unittest.main()
