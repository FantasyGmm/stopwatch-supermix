#include "apps/app_codex_status/codex_pending_glow_logic.h"

#include <cassert>

using codex_pending_glow::Transition;

int main()
{
    using codex_pending_glow::desired_active;
    using codex_pending_glow::transition_for;

    assert(desired_active(CodexTaskState::Pending, false));
    assert(!desired_active(CodexTaskState::Pending, true));
    assert(!desired_active(CodexTaskState::Running, false));
    assert(!desired_active(CodexTaskState::Completed, false));

    assert(transition_for(false, false) == Transition::None);
    assert(transition_for(false, true) == Transition::Start);
    assert(transition_for(true, true) == Transition::None);
    assert(transition_for(true, false) == Transition::Stop);
    return 0;
}
