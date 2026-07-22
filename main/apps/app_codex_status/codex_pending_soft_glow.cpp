/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "codex_pending_soft_glow.h"

#include "codex_pending_glow_logic.h"
#include "codex_status_ui_layout.h"

namespace {

lv_obj_t* make_ring(lv_obj_t* parent, int x, int y, int size, int border_width)
{
    lv_obj_t* ring = lv_obj_create(parent);
    lv_obj_set_size(ring, size, size);
    lv_obj_set_pos(ring, x, y);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(ring, 0, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, border_width, 0);
    lv_obj_set_style_border_color(
        ring,
        lv_color_hex(codex_status_ui::kAlertVermilion),
        0
    );
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
    return ring;
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

    _rings[0] = make_ring(parent, 1, 1, 464, 2);
    _rings[1] = make_ring(parent, 5, 5, 456, 4);
    _rings[2] = make_ring(parent, 11, 11, 444, 6);
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
    for (lv_obj_t*& ring : _rings) {
        if (ring != nullptr) {
            lv_obj_delete(ring);
            ring = nullptr;
        }
    }
    _heading = nullptr;
}

bool CodexPendingSoftGlow::active() const
{
    return _active;
}

void CodexPendingSoftGlow::apply_phase(int32_t phase)
{
    const lv_opa_t core = interpolate_opa(phase, LV_OPA_40, LV_OPA_COVER);
    const lv_opa_t middle = interpolate_opa(phase, LV_OPA_20, LV_OPA_60);
    const lv_opa_t inner = interpolate_opa(phase, LV_OPA_10, LV_OPA_30);
    const lv_opa_t heading = interpolate_opa(phase, LV_OPA_70, LV_OPA_COVER);

    if (_rings[0] != nullptr) {
        lv_obj_set_style_border_opa(_rings[0], core, 0);
    }
    if (_rings[1] != nullptr) {
        lv_obj_set_style_border_opa(_rings[1], middle, 0);
    }
    if (_rings[2] != nullptr) {
        lv_obj_set_style_border_opa(_rings[2], inner, 0);
    }
    if (_heading != nullptr) {
        lv_obj_set_style_opa(_heading, heading, 0);
    }
}

void CodexPendingSoftGlow::start()
{
    _active = true;
    for (lv_obj_t* ring : _rings) {
        if (ring != nullptr) {
            lv_obj_remove_flag(ring, LV_OBJ_FLAG_HIDDEN);
        }
    }
    apply_phase(0);

    lv_anim_delete(this, animation_cb);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, this);
    lv_anim_set_exec_cb(&animation, animation_cb);
    lv_anim_set_values(&animation, 0, 255);
    lv_anim_set_duration(&animation, 900);
    lv_anim_set_playback_duration(&animation, 900);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}

void CodexPendingSoftGlow::stop()
{
    lv_anim_delete(this, animation_cb);
    for (lv_obj_t* ring : _rings) {
        if (ring != nullptr) {
            lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
        }
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
