/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <lvgl.h>

class CodexPendingSoftGlow {
public:
    void init(lv_obj_t* parent);
    void set_heading(lv_obj_t* label);
    void set_active(bool active);
    void shutdown();
    bool active() const;

private:
    lv_obj_t* _overlay = nullptr;
    lv_obj_t* _heading = nullptr;
    int32_t _phase = 0;
    bool _active = false;

    void apply_phase(int32_t phase);
    void invalidate_ring_segments();
    void start();
    void stop();
    static void animation_cb(void* target, int32_t phase);
    static void draw_event_cb(lv_event_t* event);
};
