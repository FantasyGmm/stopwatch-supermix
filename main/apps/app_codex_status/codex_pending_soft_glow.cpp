/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "codex_pending_soft_glow.h"

#include "codex_pending_glow_logic.h"
#include "codex_status_ui_layout.h"

namespace {

void draw_ring(
    lv_layer_t* layer,
    const lv_area_t& overlay_coords,
    const codex_pending_glow::RingGeometry& ring,
    lv_opa_t opacity
)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_TRANSP;
    dsc.border_color = lv_color_hex(codex_status_ui::kAlertVermilion);
    dsc.border_opa = opacity;
    dsc.border_width = ring.border_width;
    dsc.radius = LV_RADIUS_CIRCLE;

    lv_area_t area = {
        static_cast<lv_coord_t>(overlay_coords.x1 + ring.inset),
        static_cast<lv_coord_t>(overlay_coords.y1 + ring.inset),
        static_cast<lv_coord_t>(
            overlay_coords.x1 + ring.inset + ring.size - 1
        ),
        static_cast<lv_coord_t>(
            overlay_coords.y1 + ring.inset + ring.size - 1
        ),
    };
    lv_draw_rect(layer, &dsc, &area);
}

lv_opa_t interpolate_opa(int32_t phase, lv_opa_t minimum, lv_opa_t maximum)
{
    return static_cast<lv_opa_t>(
        minimum + phase * (maximum - minimum) / 255
    );
}

}  // namespace

void CodexPendingSoftGlow::init(lv_obj_t* parent)
{
    shutdown();
    if (parent == nullptr) {
        return;
    }

    _overlay = lv_obj_create(parent);
    lv_obj_set_size(
        _overlay,
        codex_pending_glow::kPanelSize,
        codex_pending_glow::kPanelSize
    );
    lv_obj_set_pos(_overlay, 0, 0);
    lv_obj_set_style_radius(_overlay, 0, 0);
    lv_obj_set_style_pad_all(_overlay, 0, 0);
    lv_obj_set_style_bg_opa(_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_overlay, 0, 0);
    lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_overlay, draw_event_cb, LV_EVENT_ALL, this);
}

void CodexPendingSoftGlow::set_heading(lv_obj_t* label)
{
    _heading = label;
    if (!_active && _heading != nullptr) {
        lv_obj_set_style_opa(_heading, LV_OPA_COVER, 0);
    }
}

void CodexPendingSoftGlow::set_active(bool active)
{
    switch (codex_pending_glow::transition_for(_active, active)) {
        case codex_pending_glow::Transition::Start:
            start();
            break;
        case codex_pending_glow::Transition::Stop:
            stop();
            break;
        case codex_pending_glow::Transition::None:
            break;
    }
}

void CodexPendingSoftGlow::shutdown()
{
    stop();
    if (_overlay != nullptr) {
        lv_obj_remove_event_cb_with_user_data(_overlay, draw_event_cb, this);
        lv_obj_delete(_overlay);
        _overlay = nullptr;
    }
    _heading = nullptr;
}

bool CodexPendingSoftGlow::active() const
{
    return _active;
}

void CodexPendingSoftGlow::apply_phase(int32_t phase)
{
    _opacity = static_cast<lv_opa_t>(
        codex_pending_glow::opacity_for_phase(phase)
    );
    if (_heading != nullptr) {
        const lv_opa_t heading = interpolate_opa(
            phase,
            LV_OPA_70,
            LV_OPA_COVER
        );
        lv_obj_set_style_opa(_heading, heading, 0);
    }
    invalidate_edges();
}

void CodexPendingSoftGlow::invalidate_edges()
{
    if (_overlay == nullptr) {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(_overlay, &coords);
    for (const auto& edge : codex_pending_glow::kInvalidationAreas) {
        lv_area_t area = {
            static_cast<lv_coord_t>(coords.x1 + edge.x1),
            static_cast<lv_coord_t>(coords.y1 + edge.y1),
            static_cast<lv_coord_t>(coords.x1 + edge.x2),
            static_cast<lv_coord_t>(coords.y1 + edge.y2),
        };
        lv_obj_invalidate_area(_overlay, &area);
    }
}

void CodexPendingSoftGlow::start()
{
    _active = true;
    if (_overlay != nullptr) {
        lv_obj_remove_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    apply_phase(0);

    lv_anim_delete(this, animation_cb);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, this);
    lv_anim_set_exec_cb(&animation, animation_cb);
    lv_anim_set_values(&animation, 0, 255);
    lv_anim_set_duration(&animation, codex_pending_glow::kHalfCycleMs);
    lv_anim_set_playback_duration(&animation, codex_pending_glow::kHalfCycleMs);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}

void CodexPendingSoftGlow::stop()
{
    lv_anim_delete(this, animation_cb);
    if (_overlay != nullptr) {
        lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    if (_heading != nullptr) {
        lv_obj_set_style_opa(_heading, LV_OPA_COVER, 0);
    }
    _active = false;
}

void CodexPendingSoftGlow::animation_cb(void* target, int32_t phase)
{
    auto* self = static_cast<CodexPendingSoftGlow*>(target);
    if (self != nullptr) {
        self->apply_phase(phase);
    }
}

void CodexPendingSoftGlow::draw_event_cb(lv_event_t* event)
{
    auto* self = static_cast<CodexPendingSoftGlow*>(
        lv_event_get_user_data(event)
    );
    if (self == nullptr) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_COVER_CHECK) {
        lv_event_set_cover_res(event, LV_COVER_RES_NOT_COVER);
        return;
    }
    if (code != LV_EVENT_DRAW_MAIN_BEGIN || !self->_active) {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(lv_event_get_target_obj(event), &coords);
    lv_layer_t* layer = lv_event_get_layer(event);
    for (const auto& ring : codex_pending_glow::kRings) {
        draw_ring(layer, coords, ring, self->_opacity);
    }
}
