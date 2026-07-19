#include "runtime/assistant/assistant_trace.h"

#include <cassert>
#include <string_view>

int main()
{
    AssistantTraceBuffer<3> trace;
    trace.record({.session_id = 7, .event = AssistantTraceEvent::Open});
    trace.record({.session_id = 7, .event = AssistantTraceEvent::StartRequest});
    trace.record({.session_id = 7, .event = AssistantTraceEvent::StateTransition});
    trace.record({.session_id = 7, .event = AssistantTraceEvent::Release});

    assert(trace.size() == 3);
    assert(trace.dropped() == 1);
    assert(trace.at(0).event == AssistantTraceEvent::StartRequest);
    assert(trace.at(0).sequence == 2);
    assert(trace.at(2).event == AssistantTraceEvent::Release);
    assert(trace.at(2).sequence == 4);

    assert(std::string_view(AssistantTraceName(AssistantTraceComponent::Runtime)) == "runtime");
    assert(std::string_view(AssistantTraceName(AssistantTraceEvent::CloseRequest)) == "close-request");
    assert(std::string_view(AssistantTraceName(AssistantTraceDetail::PhysicalHome)) == "physical-home");
    assert(std::string_view(AssistantTraceName(AssistantTraceResult::TimedOut)) == "timed-out");
    assert(std::string_view(AssistantTraceName(AssistantRuntimeState::Stopping)) == "stopping");

    trace.clear();
    assert(trace.size() == 0);
    assert(trace.dropped() == 0);
    trace.record({.session_id = 8, .event = AssistantTraceEvent::Open});
    assert(trace.at(0).sequence == 1);
    return 0;
}
