import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class CodexPendingGlowFirmwareContractTests(unittest.TestCase):
    def test_soft_glow_uses_one_custom_overlay_and_one_shared_phase(self):
        header = (
            ROOT / "main/apps/app_codex_status/codex_pending_soft_glow.h"
        ).read_text()
        source = (
            ROOT / "main/apps/app_codex_status/codex_pending_soft_glow.cpp"
        ).read_text()
        logic = (
            ROOT / "main/apps/app_codex_status/codex_pending_glow_logic.h"
        ).read_text()

        self.assertIn("lv_obj_t* _overlay", header)
        self.assertIn("lv_opa_t _opacity", header)
        self.assertNotIn("std::array<lv_obj_t*, 3> _rings", header)
        self.assertIn("static void draw_event_cb", header)
        self.assertIn("void invalidate_edges()", header)
        self.assertIn("LV_EVENT_DRAW_MAIN_BEGIN", source)
        self.assertIn(
            "for (const auto& ring : codex_pending_glow::kRings)", source
        )
        self.assertIn("lv_draw_rect(layer, &dsc, &area)", source)
        self.assertEqual(source.count("_opacity ="), 1)
        self.assertIn("codex_pending_glow::opacity_for_phase(phase)", source)
        self.assertIn("constexpr int kHalfCycleMs = 600", logic)
        self.assertIn(
            "constexpr int kFullCycleMs = kHalfCycleMs * 2", logic
        )
        self.assertIn(
            "lv_anim_set_duration(&animation, "
            "codex_pending_glow::kHalfCycleMs)",
            source,
        )
        self.assertIn(
            "lv_anim_set_playback_duration(&animation, "
            "codex_pending_glow::kHalfCycleMs)",
            source,
        )
        self.assertIn(
            "lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE)", source
        )
        self.assertIn(
            "lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out)", source
        )
    def test_animation_invalidates_only_edge_bands_and_restores_heading(self):
        source = (
            ROOT / "main/apps/app_codex_status/codex_pending_soft_glow.cpp"
        ).read_text()

        self.assertIn(
            "void CodexPendingSoftGlow::invalidate_edges()", source
        )
        invalidate_body = source.split(
            "void CodexPendingSoftGlow::invalidate_edges()", 1
        )[1]
        invalidate_body = invalidate_body.split(
            "void CodexPendingSoftGlow::start()", 1
        )[0]
        self.assertIn(
            "codex_pending_glow::kInvalidationAreas", invalidate_body
        )
        self.assertIn(
            "lv_obj_invalidate_area(_overlay, &area)", invalidate_body
        )
        self.assertNotIn("lv_obj_invalidate(_overlay)", invalidate_body)

        phase_body = source.split(
            "void CodexPendingSoftGlow::apply_phase", 1
        )[1]
        phase_body = phase_body.split(
            "void CodexPendingSoftGlow::invalidate_edges", 1
        )[0]
        self.assertNotIn("lv_obj_set_style_border_opa", phase_body)
        self.assertIn("invalidate_edges()", phase_body)
        self.assertIn("lv_anim_delete(this, animation_cb)", source)
        self.assertIn(
            "lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN)", source
        )
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
            app.index("_pending_glow->init(_codex_page)"),
            app.index("lv_image_create(_codex_page)"),
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
