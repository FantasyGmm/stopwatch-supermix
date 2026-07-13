/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "runtime_diagnostics.h"

#ifndef STOPWATCH_HOST_TEST
#include "assistant/assistant_runtime.h"
#include "hal/hal.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

RuntimeDiagnosticDelta CompareRuntimeSnapshots(const RuntimeDiagnosticSnapshot& before,
                                                const RuntimeDiagnosticSnapshot& after)
{
    return {
        static_cast<std::ptrdiff_t>(before.internal_free_heap) -
            static_cast<std::ptrdiff_t>(after.internal_free_heap),
        static_cast<std::ptrdiff_t>(before.psram_free) -
            static_cast<std::ptrdiff_t>(after.psram_free),
        static_cast<std::ptrdiff_t>(after.task_count) -
            static_cast<std::ptrdiff_t>(before.task_count),
    };
}

bool PassesSingleSessionLeakGate(const RuntimeDiagnosticDelta& delta)
{
    return delta.internal_heap_loss <= 4096 && delta.task_delta == 0;
}

RuntimeDiagnosticSnapshot CaptureRuntimeDiagnosticSnapshot()
{
#ifdef STOPWATCH_HOST_TEST
    return {};
#else
    auto& assistant = GetAssistantRuntime();
    return {
        .internal_free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        .internal_min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        .psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        .task_count = static_cast<std::size_t>(uxTaskGetNumberOfTasks()),
        .resources = GetResourceSupervisor().snapshot(),
        .assistant_state = assistant.state(),
        .assistant_bindings = assistant.bindingCount(),
        .assistant_subscriptions = assistant.subscriptionCount(),
        .captured_at_ms = GetHAL().millis(),
    };
#endif
}
