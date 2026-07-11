/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <smooth_lvgl.hpp>
#include <memory>
#include <cstdint>

class AppAssistant : public mooncake::AppAbility {
public:
    AppAssistant();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _message_label;
    uint32_t _switch_at_ms = 0;
    bool _switch_started   = false;

    void create_switch_view();
    void set_message(const char* message);
    void switch_to_assistant();
};
