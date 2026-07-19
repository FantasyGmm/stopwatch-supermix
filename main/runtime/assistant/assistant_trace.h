#pragma once

#include "assistant_state.h"

#if !defined(STOPWATCH_HOST_TEST)
#include <sdkconfig.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>

enum class AssistantTraceComponent : uint8_t {
    App,
    Runtime,
    Callback,
    Resource,
    Button,
    Debug,
};

enum class AssistantTraceEvent : uint8_t {
    Open,
    StartRequest,
    StateTransition,
    ResourceDecision,
    ActionSubmit,
    Callback,
    CloseRequest,
    StopTimeout,
    Release,
    Closed,
    Error,
    Conversation,
    Snapshot,
    ForceHome,
};

enum class AssistantTraceDetail : uint8_t {
    None,
    PhysicalHome,
    DebugHome,
    ForceHome,
    Start,
    Activate,
    Running,
    Stop,
    Stopped,
    Wifi,
    Capture,
    Playback,
    AllResources,
    BeforeStart,
    FirstRunning,
    StopBoundary,
    StaleSession,
    Unexpected,
};

enum class AssistantTraceResult : uint8_t {
    None,
    Accepted,
    Rejected,
    Ok,
    Failed,
    Busy,
    TimedOut,
    Suppressed,
    Duplicate,
};

struct AssistantTraceRecord {
    uint64_t session_id = 0;
    uint32_t sequence = 0;
    uint32_t timestamp_ms = 0;
    AssistantTraceComponent component = AssistantTraceComponent::Runtime;
    AssistantTraceEvent event = AssistantTraceEvent::StateTransition;
    AssistantRuntimeState state = AssistantRuntimeState::Stopped;
    AssistantTraceDetail detail = AssistantTraceDetail::None;
    AssistantTraceResult result = AssistantTraceResult::None;
    int32_t value0 = 0;
    int32_t value1 = 0;
    int32_t value2 = 0;
    int32_t value3 = 0;
};

template <std::size_t Capacity>
class AssistantTraceBuffer {
public:
    static_assert(Capacity > 0, "Assistant trace capacity must be positive");

    void record(AssistantTraceRecord record)
    {
        record.sequence = ++_next_sequence;
        if (_size == Capacity) {
            _start = (_start + 1) % Capacity;
            ++_dropped;
        } else {
            ++_size;
        }
        _records[(_start + _size - 1) % Capacity] = record;
    }

    const AssistantTraceRecord& at(std::size_t index) const
    {
        return _records[(_start + index) % Capacity];
    }

    std::size_t size() const { return _size; }
    uint32_t dropped() const { return _dropped; }

    void clear()
    {
        _records = {};
        _start = 0;
        _size = 0;
        _next_sequence = 0;
        _dropped = 0;
    }

private:
    std::array<AssistantTraceRecord, Capacity> _records{};
    std::size_t _start = 0;
    std::size_t _size = 0;
    uint32_t _next_sequence = 0;
    uint32_t _dropped = 0;
};

const char* AssistantTraceName(AssistantTraceComponent value);
const char* AssistantTraceName(AssistantTraceEvent value);
const char* AssistantTraceName(AssistantTraceDetail value);
const char* AssistantTraceName(AssistantTraceResult value);
const char* AssistantTraceName(AssistantRuntimeState value);

class AssistantTrace {
public:
    void record(AssistantTraceRecord record);
    void flush();
    void dump();
    void clear();
    std::size_t size() const;
    uint32_t dropped() const;
    bool read(std::size_t index, AssistantTraceRecord& record) const;

private:
#if defined(CONFIG_STOPWATCH_DEBUG_COMMANDS) && CONFIG_STOPWATCH_DEBUG_COMMANDS
    AssistantTraceBuffer<64> _buffer;
    uint32_t _rendered_sequence = 0;
#endif
};

AssistantTrace& GetAssistantTrace();
