/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "view/view.h"
#include <sdkconfig.h>
#if CONFIG_STOPWATCH_DEBUG_COMMANDS
#include <debug/debug_app_control.h>
#endif
#include <mooncake.h>
#include <mooncake_templates.h>
#include <cstdint>
#include <memory>

class AppLauncher : public mooncake::templates::AppLauncherBase
#if CONFIG_STOPWATCH_DEBUG_COMMANDS
                  , public DebugAppTarget
#endif
{
public:
    void onLauncherCreate() override;
    void onLauncherOpen() override;
    void onLauncherRunning() override;
    void onLauncherClose() override;
    void onLauncherDestroy() override;
#if CONFIG_STOPWATCH_DEBUG_COMMANDS
    DebugAppOpenResult openDebugApp(std::string_view name) override;
    void logDebugAppList() override;
    bool goDebugHome() override;
#endif

private:
    std::unique_ptr<view::LauncherView> _view;
    std::unique_ptr<view::GuidePage> _guide_page;
    std::unique_ptr<input::KeyManager> _guide_key_manager;
    bool _is_first_open              = true;
    bool _pending_status_bar_create  = false;
    uint32_t _status_bar_create_tick = 0;
    uint32_t _last_charge_check_tick = 0;
    bool _was_battery_charging       = false;
    bool _should_play_boot_sfx       = true;

    void create_launcher_view();
    void show_guide_page();
    bool update_guide_page();
    void finish_launcher_open();
};
