/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "runtime_supervisor.h"

#include "assistant/assistant_trace.h"
#include "service_runtime.h"

#include <mooncake_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
RuntimeSupervisor runtime_supervisor;
constexpr const char* tag = "RuntimeSupervisor";

void trace_snapshot(uint64_t session_id, AssistantTraceDetail detail,
                    const RuntimeDiagnosticSnapshot& snapshot,
                    AssistantTraceResult result = AssistantTraceResult::Ok)
{
    GetAssistantTrace().record({
        .session_id = session_id,
        .timestamp_ms = snapshot.captured_at_ms,
        .component = AssistantTraceComponent::Runtime,
        .event = AssistantTraceEvent::Snapshot,
        .state = snapshot.assistant_state,
        .detail = detail,
        .result = result,
        .value0 = static_cast<int32_t>(snapshot.internal_free_heap),
        .value1 = static_cast<int32_t>(snapshot.psram_free),
        .value2 = static_cast<int32_t>(snapshot.task_count),
        .value3 = static_cast<int32_t>(snapshot.resources.playback_owner),
    });
}
} // namespace

RuntimeSupervisor& GetRuntimeSupervisor()
{
    return runtime_supervisor;
}

bool RuntimeSupervisor::start()
{
    if (_started) {
        return onRuntimeThread();
    }
    if (!GetServiceRuntime().isStarted()) {
        return false;
    }

    _runtime_thread = xTaskGetCurrentTaskHandle();
    _last_assistant_state = GetAssistantRuntime().state();
    _started = true;
    mclog::tagInfo(tag, "runtime facade started");
    return true;
}

AssistantStartResult RuntimeSupervisor::startAssistant()
{
    if (!_started || !onRuntimeThread()) {
        return AssistantStartResult::ServiceUnavailable;
    }

    auto& assistant = GetAssistantRuntime();
    if (assistant.state() != AssistantRuntimeState::Stopped) {
        return AssistantStartResult::AlreadyActive;
    }

    _before_snapshot = CaptureRuntimeDiagnosticSnapshot();
    logSnapshot("before-start", _before_snapshot);
    const uint64_t previous_session_id = assistant.sessionId();
    trace_snapshot(previous_session_id + 1, AssistantTraceDetail::BeforeStart,
                   _before_snapshot);
    const AssistantStartResult result = assistant.start();
    _last_assistant_state = assistant.state();
    GetAssistantTrace().record({
        .session_id = assistant.sessionId(),
        .timestamp_ms = _before_snapshot.captured_at_ms,
        .component = AssistantTraceComponent::Runtime,
        .event = AssistantTraceEvent::StartRequest,
        .state = _last_assistant_state,
        .detail = AssistantTraceDetail::Start,
        .result = result == AssistantStartResult::Submitted
                      ? AssistantTraceResult::Accepted
                      : AssistantTraceResult::Rejected,
        .value0 = static_cast<int32_t>(result),
    });

    if (assistant.sessionId() != previous_session_id) {
        _tracked_session_id = assistant.sessionId();
        _session_pending = true;
        _running_captured = false;
        _stopped_captured = false;
        if (_last_assistant_state == AssistantRuntimeState::Stopped) {
            finishStoppedBoundary();
        }
    }
    return result;
}

bool RuntimeSupervisor::requestStopAssistant()
{
    return _started && onRuntimeThread() && GetAssistantRuntime().requestStop();
}

bool RuntimeSupervisor::requestAssistantVolumeSync(int volume)
{
    if (!_started || !onRuntimeThread()) {
        return false;
    }
    if (GetAssistantRuntime().state() != AssistantRuntimeState::Running) {
        return true;
    }
    return GetAssistantRuntime().requestOutputVolumeSync(volume);
}

void RuntimeSupervisor::update(uint32_t now_ms)
{
    if (!_started || !onRuntimeThread()) {
        return;
    }

    auto& assistant = GetAssistantRuntime();
    assistant.update(now_ms);
    const AssistantRuntimeState current = assistant.state();

    if (_session_pending && assistant.sessionId() == _tracked_session_id) {
        if (current == AssistantRuntimeState::Running && !_running_captured) {
            _running_snapshot = CaptureRuntimeDiagnosticSnapshot();
            _running_captured = true;
            logSnapshot("first-running", _running_snapshot);
            trace_snapshot(_tracked_session_id, AssistantTraceDetail::FirstRunning,
                           _running_snapshot);
        }
        if (current == AssistantRuntimeState::Stopped &&
            _last_assistant_state != AssistantRuntimeState::Stopped) {
            finishStoppedBoundary();
        }
    }
    _last_assistant_state = current;
    GetAssistantTrace().flush();
}

AssistantRuntimeState RuntimeSupervisor::assistantState() const
{
    return GetAssistantRuntime().state();
}

bool RuntimeSupervisor::pollAssistantEvent(AssistantEvent& event)
{
    return _started && onRuntimeThread() && GetAssistantRuntime().pollEvent(event);
}

RuntimeDiagnosticSnapshot RuntimeSupervisor::diagnostics() const
{
    if (!_started || !onRuntimeThread()) {
        return {};
    }
    const RuntimeDiagnosticSnapshot snapshot = CaptureRuntimeDiagnosticSnapshot();
    logSnapshot("explicit", snapshot);
    return snapshot;
}

bool RuntimeSupervisor::onRuntimeThread() const
{
    return _runtime_thread != nullptr && _runtime_thread == xTaskGetCurrentTaskHandle();
}

void RuntimeSupervisor::logSnapshot(const char* boundary,
                                    const RuntimeDiagnosticSnapshot& snapshot)
{
    mclog::tagInfo(tag,
                   "diagnostic {} t={} internal={}/{} largest={} psram={} tasks={} state={} bindings={} "
                   "subscriptions={} wifi={} capture={} playback={} ap={} blocked={}",
                   boundary, snapshot.captured_at_ms, snapshot.internal_free_heap,
                   snapshot.internal_min_free_heap, snapshot.internal_largest_free_block,
                   snapshot.psram_free, snapshot.task_count,
                   static_cast<unsigned>(snapshot.assistant_state), snapshot.assistant_bindings,
                   snapshot.assistant_subscriptions, snapshot.resources.wifi_lease_count,
                   static_cast<unsigned>(snapshot.resources.capture_owner),
                   static_cast<unsigned>(snapshot.resources.playback_owner),
                   snapshot.resources.wifi_ap_active, snapshot.resources.blocked);
}

void RuntimeSupervisor::finishStoppedBoundary()
{
    if (_stopped_captured) {
        return;
    }

    _stopped_snapshot = CaptureRuntimeDiagnosticSnapshot();
    _stopped_captured = true;
    logSnapshot("stopped", _stopped_snapshot);
    const RuntimeDiagnosticDelta delta = CompareRuntimeSnapshots(_before_snapshot, _stopped_snapshot);
    GetAssistantTrace().record({
        .session_id = _tracked_session_id,
        .timestamp_ms = _stopped_snapshot.captured_at_ms,
        .component = AssistantTraceComponent::Runtime,
        .event = AssistantTraceEvent::Snapshot,
        .state = _stopped_snapshot.assistant_state,
        .detail = AssistantTraceDetail::StopBoundary,
        .result = PassesSingleSessionLeakGate(delta)
                      ? AssistantTraceResult::Ok
                      : AssistantTraceResult::Failed,
        .value0 = static_cast<int32_t>(delta.internal_heap_loss),
        .value1 = static_cast<int32_t>(delta.psram_loss),
        .value2 = static_cast<int32_t>(delta.task_delta),
        .value3 = static_cast<int32_t>(_stopped_snapshot.resources.playback_owner),
    });
    mclog::tagInfo(tag, "session {} stopped: internal_loss={} psram_loss={} task_delta={} gate={}",
                   _tracked_session_id, delta.internal_heap_loss, delta.psram_loss,
                   delta.task_delta, PassesSingleSessionLeakGate(delta));
    _session_pending = false;
}
