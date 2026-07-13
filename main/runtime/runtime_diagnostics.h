/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "assistant/assistant_state.h"
#include "resource_supervisor.h"

#include <cstddef>
#include <cstdint>

struct RuntimeDiagnosticSnapshot {
    std::size_t internal_free_heap = 0;
    std::size_t internal_min_free_heap = 0;
    std::size_t psram_free = 0;
    std::size_t task_count = 0;
    ResourceSnapshot resources;
    AssistantRuntimeState assistant_state = AssistantRuntimeState::Stopped;
    std::size_t assistant_bindings = 0;
    std::size_t assistant_subscriptions = 0;
    uint32_t captured_at_ms = 0;
};

struct RuntimeDiagnosticDelta {
    std::ptrdiff_t internal_heap_loss = 0;
    std::ptrdiff_t psram_loss = 0;
    std::ptrdiff_t task_delta = 0;
};

RuntimeDiagnosticDelta CompareRuntimeSnapshots(const RuntimeDiagnosticSnapshot& before,
                                                const RuntimeDiagnosticSnapshot& after);
bool PassesSingleSessionLeakGate(const RuntimeDiagnosticDelta& delta);
RuntimeDiagnosticSnapshot CaptureRuntimeDiagnosticSnapshot();
