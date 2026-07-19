#include "debug_app_control.h"

namespace {
DebugAppTarget* active_target = nullptr;
} // namespace

bool RegisterDebugAppTarget(DebugAppTarget* target)
{
    if (target == nullptr || active_target != nullptr) {
        return false;
    }
    active_target = target;
    return true;
}

void UnregisterDebugAppTarget(DebugAppTarget* target)
{
    if (active_target == target) {
        active_target = nullptr;
    }
}

DebugAppOpenResult RequestDebugAppOpen(std::string_view name)
{
    return active_target == nullptr ? DebugAppOpenResult::Busy
                                    : active_target->openDebugApp(name);
}

bool RequestDebugAppList()
{
    if (active_target == nullptr) {
        return false;
    }
    active_target->logDebugAppList();
    return true;
}

bool RequestDebugAppHome()
{
    return active_target != nullptr && active_target->goDebugHome();
}
