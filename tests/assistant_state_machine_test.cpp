#include "runtime/assistant/assistant_state.h"
#include "runtime/assistant/assistant_stop_policy.h"

#include <cassert>

int main()
{
    assert(DecideAssistantStopPath(false, true) == AssistantStopPath::SubmitAgentStop);
    assert(DecideAssistantStopPath(false, false) == AssistantStopPath::WaitForOfflineAgentStop);
    assert(DecideAssistantStopPath(true, true) == AssistantStopPath::ReleasePendingSetup);

    AssistantStateMachine state;
    assert(state.state() == AssistantRuntimeState::Stopped);
    assert(state.requestStart() == TransitionResult::Accepted);
    const auto first = state.sessionId();
    assert(first == 1);
    assert(state.requestStart() == TransitionResult::Rejected);
    assert(state.markRunning(first) == TransitionResult::Accepted);
    assert(state.requestStop() == TransitionResult::Accepted);
    assert(state.requestStop() == TransitionResult::AlreadyApplied);
    assert(state.markStopTimedOut(first) == TransitionResult::Accepted);
    assert(state.resourcesBlocked());
    assert(state.markStopped(first - 1) == TransitionResult::Rejected);
    assert(state.markStopped(first) == TransitionResult::Accepted);
    assert(!state.resourcesBlocked());

    assert(state.requestStart() == TransitionResult::Accepted);
    const auto second = state.sessionId();
    assert(second == 2);
    assert(state.markFailed(second) == TransitionResult::Accepted);
    assert(state.requestStop() == TransitionResult::Accepted);
    assert(state.markStopped(second) == TransitionResult::Accepted);

    assert(state.requestStart() == TransitionResult::Accepted);
    const auto third = state.sessionId();
    assert(third == 3);
    assert(state.markRunning(second) == TransitionResult::Rejected);
    assert(state.requestStop() == TransitionResult::Accepted);
    assert(state.markRunning(third) == TransitionResult::Rejected);
    assert(state.markStopped(third) == TransitionResult::Accepted);
    return 0;
}
