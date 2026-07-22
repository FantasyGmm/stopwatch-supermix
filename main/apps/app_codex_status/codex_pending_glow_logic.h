#pragma once

#include "codex_status_model.h"

namespace codex_pending_glow {

enum class Transition {
    None,
    Start,
    Stop,
};

constexpr bool desired_active(CodexTaskState state, bool screen_off)
{
    return state == CodexTaskState::Pending && !screen_off;
}

constexpr Transition transition_for(bool active, bool requested_active)
{
    if (active == requested_active) {
        return Transition::None;
    }
    return requested_active ? Transition::Start : Transition::Stop;
}

}  // namespace codex_pending_glow
