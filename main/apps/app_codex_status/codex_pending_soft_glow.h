/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <array>
#include <lvgl.h>

class CodexPendingSoftGlow {
public:
    void init(lv_obj_t* parent);
    void set_heading(lv_obj_t* label);
    void set_active(bool active);
    void shutdown();
    bool active() const;

private:
    std::array<lv_obj_t*, 3> _rings{};
    lv_obj_t* _heading = nullptr;
    bool _active = false;

    void apply_phase(int32_t phase);
    void start();
    void stop();
    static void animation_cb(void* target, int32_t phase);
};
