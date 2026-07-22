/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include <memory>
#include <smooth_lvgl.hpp>

enum class AssistantViewState : uint8_t {
    Connecting,
    Activating,
    Listening,
    Thinking,
    Speaking,
    Failed,
    Closing,
    StopTimedOut,
};

class AssistantView {
public:
    void init(lv_obj_t* parent);
    void setState(AssistantViewState state);
    void setPrimaryText(const char* text);
    void setSecondaryText(const char* text);
    void setUpdateVersion(const char* canonical_version);
    void destroy();

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _primary_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _secondary_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _hint_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _update_label;
};
