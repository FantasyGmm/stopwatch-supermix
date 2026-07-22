/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../assistant_presentation_model.h"

#include <array>
#include <cstdint>
#include <memory>
#include <smooth_lvgl.hpp>

using AssistantViewState = AssistantVisualState;

class AssistantView {
public:
    void init(lv_obj_t* parent);
    void update(uint32_t now_ms);
    void setState(AssistantVisualState state);
    void setConversationText(const char* text);
    void setUpdateVersion(const char* canonical_version);
    void setBattery(uint8_t percent, bool charging);
    void setConnected(bool connected);
    void setPrimaryText(const char* text);
    void setSecondaryText(const char* text);
    void destroy();

private:
    void applyDefaultCopy();
    void applyWaveFrame(uint8_t frame_index);
    void applyWaveColor(uint32_t color);

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _network_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _update_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _phase_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _content_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _hint_label;
    std::array<lv_obj_t*, 9> _wave_bars{};
    lv_obj_t* _wave_container = nullptr;
    lv_obj_t* _divider = nullptr;
    lv_obj_t* _network_dot = nullptr;
    lv_obj_t* _battery_body = nullptr;
    lv_obj_t* _battery_tip = nullptr;
    lv_obj_t* _battery_label = nullptr;
    AssistantVisualState _state = AssistantVisualState::Connecting;
    uint32_t _last_animation_ms = 0;
    uint8_t _frame_index = 0;
};
