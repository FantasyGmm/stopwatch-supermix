/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps/apps.h>
#include <apps/common/edge_control/global_edge_control_service.h>
#include <hal/hal.h>
#include <runtime/service_runtime.h>
#include <runtime/runtime_supervisor.h>
#include <sdkconfig.h>
#if CONFIG_STOPWATCH_DEBUG_COMMANDS
#include <debug/debug_command_service.h>
#endif

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    // Setup logger
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    // HAL init
    GetHAL().init();

    // Shared service runtime init
    if (!GetServiceRuntime().start()) {
        mclog::tagError("Main", "service runtime unavailable; application startup aborted");
        return;
    }
    if (!GetRuntimeSupervisor().start()) {
        mclog::tagError("Main", "runtime supervisor unavailable; application startup aborted");
        return;
    }

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    {
        LvglLockGuard lock;
        if (!GetGlobalEdgeControlService().init()) {
            mclog::tagError("Main", "global edge controls unavailable");
        }
    }

    // Install apps
    GetMooncake().installApp(std::make_unique<AppLauncher>());
    GetMooncake().installApp(std::make_unique<AppCodexStatus>());
    GetMooncake().installApp(std::make_unique<AppAssistant>());
    GetMooncake().installApp(std::make_unique<AppClock>());
    GetMooncake().installApp(std::make_unique<AppBadge>());
    GetMooncake().installApp(std::make_unique<AppImu>());
    GetMooncake().installApp(std::make_unique<AppFft>());
    GetMooncake().installApp(std::make_unique<AppSetup>());

    // Main loop
    while (1) {
        GetHAL().feedTheDog();
        GetHAL().updatePowerLed();
#if CONFIG_STOPWATCH_DEBUG_COMMANDS
        GetDebugCommandService().update();
#endif
        GetRuntimeSupervisor().update(GetHAL().millis());
        {
            LvglLockGuard lock;
            GetGlobalEdgeControlService().update();
        }
        GetGlobalEdgeControlService().applyPendingHardwareUpdate();
        GetMooncake().update();
    }
}
