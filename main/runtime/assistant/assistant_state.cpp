/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "assistant_state.h"

TransitionResult AssistantStateMachine::requestStart()
{
    if (_state != AssistantRuntimeState::Stopped) {
        return TransitionResult::Rejected;
    }
    ++_session_id;
    _state = AssistantRuntimeState::Starting;
    return TransitionResult::Accepted;
}

TransitionResult AssistantStateMachine::markRunning(uint64_t session_id)
{
    if (!current(session_id) || _state != AssistantRuntimeState::Starting) {
        return TransitionResult::Rejected;
    }
    _state = AssistantRuntimeState::Running;
    return TransitionResult::Accepted;
}

TransitionResult AssistantStateMachine::markFailed(uint64_t session_id)
{
    if (!current(session_id) || _state != AssistantRuntimeState::Starting) {
        return TransitionResult::Rejected;
    }
    _state = AssistantRuntimeState::Failed;
    return TransitionResult::Accepted;
}

TransitionResult AssistantStateMachine::requestStop()
{
    if (_state == AssistantRuntimeState::Stopping ||
        _state == AssistantRuntimeState::StopTimedOut) {
        return TransitionResult::AlreadyApplied;
    }
    if (_state != AssistantRuntimeState::Starting &&
        _state != AssistantRuntimeState::Running &&
        _state != AssistantRuntimeState::Failed) {
        return TransitionResult::Rejected;
    }
    _state = AssistantRuntimeState::Stopping;
    return TransitionResult::Accepted;
}

TransitionResult AssistantStateMachine::markStopTimedOut(uint64_t session_id)
{
    if (!current(session_id) || _state != AssistantRuntimeState::Stopping) {
        return TransitionResult::Rejected;
    }
    _state = AssistantRuntimeState::StopTimedOut;
    return TransitionResult::Accepted;
}

TransitionResult AssistantStateMachine::markStopped(uint64_t session_id)
{
    if (!current(session_id) ||
        (_state != AssistantRuntimeState::Stopping &&
         _state != AssistantRuntimeState::StopTimedOut)) {
        return TransitionResult::Rejected;
    }
    _state = AssistantRuntimeState::Stopped;
    return TransitionResult::Accepted;
}
