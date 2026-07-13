/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "service_runtime.h"
#include "services/stopwatch_audio_device.h"
#include <brookesia/service_manager.hpp>
#include <mooncake_log.h>

namespace {
ServiceRuntime service_runtime;
constexpr const char* tag = "ServiceRuntime";
} // namespace

ServiceRuntime& GetServiceRuntime()
{
    return service_runtime;
}

bool ServiceRuntime::start()
{
    if (_started) {
        return true;
    }

    if (!InitializeStopWatchAudioDevice() || !StopWatchAudioInterfacesReady()) {
        mclog::tagError(tag, "failed to initialize StopWatch audio device");
        return false;
    }

    auto& manager = esp_brookesia::service::ServiceManager::get_instance();
    if (!manager.start()) {
        mclog::tagError(tag, "failed to start Brookesia service manager");
        return false;
    }

    _started = true;
    mclog::tagInfo(tag, "Brookesia service manager started");
    return true;
}
