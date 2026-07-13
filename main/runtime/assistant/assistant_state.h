/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>

enum class AssistantRuntimeState : uint8_t {
    Stopped,
    Starting,
    Running,
    Failed,
    Stopping,
    StopTimedOut,
};

enum class TransitionResult : uint8_t {
    Accepted,
    AlreadyApplied,
    Rejected,
};

class AssistantStateMachine {
public:
    TransitionResult requestStart();
    TransitionResult markRunning(uint64_t session_id);
    TransitionResult markFailed(uint64_t session_id);
    TransitionResult requestStop();
    TransitionResult markStopTimedOut(uint64_t session_id);
    TransitionResult markStopped(uint64_t session_id);

    AssistantRuntimeState state() const { return _state; }
    uint64_t sessionId() const { return _session_id; }
    bool resourcesBlocked() const { return _state != AssistantRuntimeState::Stopped; }

private:
    bool current(uint64_t session_id) const { return session_id == _session_id; }

    AssistantRuntimeState _state = AssistantRuntimeState::Stopped;
    uint64_t _session_id = 0;
};
