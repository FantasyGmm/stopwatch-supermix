/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "codex_status_model.h"

#include <lvgl.h>

class CodexPendingSoftGlow {
public:
    void init(lv_obj_t* parent);
    void set_status_targets(
        lv_obj_t* pending_label,
        lv_obj_t* pending_count,
        lv_obj_t* running_label,
        lv_obj_t* running_count
    );
    void set_state(CodexTaskState state, bool screen_off);
    void shutdown();
    bool active() const;

private:
    lv_obj_t* _overlay = nullptr;
    lv_obj_t* _pending_label = nullptr;
    lv_obj_t* _pending_count = nullptr;
    lv_obj_t* _running_label = nullptr;
    lv_obj_t* _running_count = nullptr;
    CodexTaskState _state = CodexTaskState::Completed;
    int32_t _phase = 0;
    bool _active = false;
    bool _ring_visible = false;

    void apply_phase(int32_t phase);
    void restore_target_opacity();
    void refresh_ring_visibility();
    void invalidate_ring_segments();
    void start();
    void stop();
    static void animation_cb(void* target, int32_t phase);
    static void draw_event_cb(lv_event_t* event);
};
