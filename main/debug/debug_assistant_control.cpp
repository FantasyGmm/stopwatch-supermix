#include "debug_assistant_control.h"

namespace {
DebugAssistantTarget* active_target = nullptr;
} // namespace

bool RegisterDebugAssistantTarget(DebugAssistantTarget* target)
{
    if (target == nullptr || active_target != nullptr) {
        return false;
    }
    active_target = target;
    return true;
}

void UnregisterDebugAssistantTarget(DebugAssistantTarget* target)
{
    if (active_target == target) {
        active_target = nullptr;
    }
}

bool RequestDebugAssistantHome()
{
    return active_target != nullptr && active_target->requestDebugGracefulHome();
}
