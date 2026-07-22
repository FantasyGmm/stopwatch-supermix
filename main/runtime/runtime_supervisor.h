/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "assistant/assistant_runtime.h"
#include "runtime_diagnostics.h"

#include <cstdint>

class RuntimeSupervisor {
public:
    bool start();
    AssistantStartResult startAssistant();
    bool requestStopAssistant();
    bool requestAssistantVolumeSync(int volume);
    void update(uint32_t now_ms);
    AssistantRuntimeState assistantState() const;
    bool pollAssistantEvent(AssistantEvent& event);
    RuntimeDiagnosticSnapshot diagnostics() const;

private:
    bool onRuntimeThread() const;
    static void logSnapshot(const char* boundary, const RuntimeDiagnosticSnapshot& snapshot);
    void finishStoppedBoundary();

    bool _started = false;
    void* _runtime_thread = nullptr;
    uint64_t _tracked_session_id = 0;
    AssistantRuntimeState _last_assistant_state = AssistantRuntimeState::Stopped;
    bool _session_pending = false;
    bool _running_captured = false;
    bool _stopped_captured = false;
    RuntimeDiagnosticSnapshot _before_snapshot;
    RuntimeDiagnosticSnapshot _running_snapshot;
    RuntimeDiagnosticSnapshot _stopped_snapshot;
};

RuntimeSupervisor& GetRuntimeSupervisor();
