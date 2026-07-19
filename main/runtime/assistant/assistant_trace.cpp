#include "assistant_trace.h"

#if defined(CONFIG_STOPWATCH_DEBUG_COMMANDS) && CONFIG_STOPWATCH_DEBUG_COMMANDS
#include <cinttypes>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

namespace {
portMUX_TYPE trace_mux = portMUX_INITIALIZER_UNLOCKED;
constexpr const char* tag = "AssistantTrace";

void render(const AssistantTraceRecord& record)
{
    ESP_LOGI(tag,
             "ASTRACE sid=%" PRIu64 " seq=%" PRIu32 " t=%" PRIu32
             " comp=%s event=%s state=%s detail=%s result=%s"
             " v0=%" PRId32 " v1=%" PRId32 " v2=%" PRId32 " v3=%" PRId32,
             record.session_id, record.sequence, record.timestamp_ms,
             AssistantTraceName(record.component), AssistantTraceName(record.event),
             AssistantTraceName(record.state), AssistantTraceName(record.detail),
             AssistantTraceName(record.result), record.value0, record.value1,
             record.value2, record.value3);
}
} // namespace
#endif

const char* AssistantTraceName(AssistantTraceComponent value)
{
    switch (value) {
        case AssistantTraceComponent::App: return "app";
        case AssistantTraceComponent::Runtime: return "runtime";
        case AssistantTraceComponent::Callback: return "callback";
        case AssistantTraceComponent::Resource: return "resource";
        case AssistantTraceComponent::Button: return "button";
        case AssistantTraceComponent::Debug: return "debug";
    }
    return "unknown";
}

const char* AssistantTraceName(AssistantTraceEvent value)
{
    switch (value) {
        case AssistantTraceEvent::Open: return "open";
        case AssistantTraceEvent::StartRequest: return "start-request";
        case AssistantTraceEvent::StateTransition: return "state";
        case AssistantTraceEvent::ResourceDecision: return "resource-decision";
        case AssistantTraceEvent::ActionSubmit: return "action-submit";
        case AssistantTraceEvent::Callback: return "callback";
        case AssistantTraceEvent::CloseRequest: return "close-request";
        case AssistantTraceEvent::StopTimeout: return "stop-timeout";
        case AssistantTraceEvent::Release: return "release";
        case AssistantTraceEvent::Closed: return "closed";
        case AssistantTraceEvent::Error: return "error";
        case AssistantTraceEvent::Conversation: return "conversation";
        case AssistantTraceEvent::Snapshot: return "snapshot";
        case AssistantTraceEvent::ForceHome: return "force-home";
    }
    return "unknown";
}

const char* AssistantTraceName(AssistantTraceDetail value)
{
    switch (value) {
        case AssistantTraceDetail::None: return "none";
        case AssistantTraceDetail::PhysicalHome: return "physical-home";
        case AssistantTraceDetail::DebugHome: return "debug-home";
        case AssistantTraceDetail::ForceHome: return "force-home";
        case AssistantTraceDetail::Start: return "start";
        case AssistantTraceDetail::Activate: return "activate";
        case AssistantTraceDetail::Running: return "running";
        case AssistantTraceDetail::Stop: return "stop";
        case AssistantTraceDetail::Stopped: return "stopped";
        case AssistantTraceDetail::Wifi: return "wifi";
        case AssistantTraceDetail::Capture: return "capture";
        case AssistantTraceDetail::Playback: return "playback";
        case AssistantTraceDetail::AllResources: return "all-resources";
        case AssistantTraceDetail::BeforeStart: return "before-start";
        case AssistantTraceDetail::FirstRunning: return "first-running";
        case AssistantTraceDetail::StopBoundary: return "stop-boundary";
        case AssistantTraceDetail::StaleSession: return "stale-session";
        case AssistantTraceDetail::Unexpected: return "unexpected";
    }
    return "unknown";
}

const char* AssistantTraceName(AssistantTraceResult value)
{
    switch (value) {
        case AssistantTraceResult::None: return "none";
        case AssistantTraceResult::Accepted: return "accepted";
        case AssistantTraceResult::Rejected: return "rejected";
        case AssistantTraceResult::Ok: return "ok";
        case AssistantTraceResult::Failed: return "failed";
        case AssistantTraceResult::Busy: return "busy";
        case AssistantTraceResult::TimedOut: return "timed-out";
        case AssistantTraceResult::Suppressed: return "suppressed";
        case AssistantTraceResult::Duplicate: return "duplicate";
    }
    return "unknown";
}

const char* AssistantTraceName(AssistantRuntimeState value)
{
    switch (value) {
        case AssistantRuntimeState::Stopped: return "stopped";
        case AssistantRuntimeState::Starting: return "starting";
        case AssistantRuntimeState::Running: return "running";
        case AssistantRuntimeState::Failed: return "failed";
        case AssistantRuntimeState::Stopping: return "stopping";
        case AssistantRuntimeState::StopTimedOut: return "stop-timed-out";
    }
    return "unknown";
}

AssistantTrace& GetAssistantTrace()
{
    static AssistantTrace trace;
    return trace;
}

void AssistantTrace::record(AssistantTraceRecord record)
{
#if defined(CONFIG_STOPWATCH_DEBUG_COMMANDS) && CONFIG_STOPWATCH_DEBUG_COMMANDS
    portENTER_CRITICAL(&trace_mux);
    _buffer.record(record);
    portEXIT_CRITICAL(&trace_mux);
#else
    (void)record;
#endif
}

void AssistantTrace::flush()
{
#if defined(CONFIG_STOPWATCH_DEBUG_COMMANDS) && CONFIG_STOPWATCH_DEBUG_COMMANDS
    const std::size_t count = size();
    for (std::size_t index = 0; index < count; ++index) {
        AssistantTraceRecord record;
        if (!read(index, record) || record.sequence <= _rendered_sequence) {
            continue;
        }
        render(record);
        _rendered_sequence = record.sequence;
    }
#endif
}

void AssistantTrace::dump()
{
#if defined(CONFIG_STOPWATCH_DEBUG_COMMANDS) && CONFIG_STOPWATCH_DEBUG_COMMANDS
    const std::size_t count = size();
    ESP_LOGI(tag, "ASTRACE DUMP count=%u dropped=%" PRIu32,
             static_cast<unsigned>(count), dropped());
    for (std::size_t index = 0; index < count; ++index) {
        AssistantTraceRecord record;
        if (read(index, record)) {
            render(record);
        }
    }
#endif
}

void AssistantTrace::clear()
{
#if defined(CONFIG_STOPWATCH_DEBUG_COMMANDS) && CONFIG_STOPWATCH_DEBUG_COMMANDS
    portENTER_CRITICAL(&trace_mux);
    _buffer.clear();
    _rendered_sequence = 0;
    portEXIT_CRITICAL(&trace_mux);
#endif
}

std::size_t AssistantTrace::size() const
{
#if defined(CONFIG_STOPWATCH_DEBUG_COMMANDS) && CONFIG_STOPWATCH_DEBUG_COMMANDS
    portENTER_CRITICAL(&trace_mux);
    const std::size_t result = _buffer.size();
    portEXIT_CRITICAL(&trace_mux);
    return result;
#else
    return 0;
#endif
}

uint32_t AssistantTrace::dropped() const
{
#if defined(CONFIG_STOPWATCH_DEBUG_COMMANDS) && CONFIG_STOPWATCH_DEBUG_COMMANDS
    portENTER_CRITICAL(&trace_mux);
    const uint32_t result = _buffer.dropped();
    portEXIT_CRITICAL(&trace_mux);
    return result;
#else
    return 0;
#endif
}

bool AssistantTrace::read(std::size_t index, AssistantTraceRecord& record) const
{
#if defined(CONFIG_STOPWATCH_DEBUG_COMMANDS) && CONFIG_STOPWATCH_DEBUG_COMMANDS
    portENTER_CRITICAL(&trace_mux);
    const bool valid = index < _buffer.size();
    if (valid) {
        record = _buffer.at(index);
    }
    portEXIT_CRITICAL(&trace_mux);
    return valid;
#else
    (void)index;
    (void)record;
    return false;
#endif
}
