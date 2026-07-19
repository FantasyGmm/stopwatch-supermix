#pragma once

#include <cstdint>
#include <string_view>

enum class DebugAppOpenResult : uint8_t {
    Ok,
    Busy,
    NotFound,
};

class DebugAppTarget {
public:
    virtual ~DebugAppTarget() = default;
    virtual DebugAppOpenResult openDebugApp(std::string_view name) = 0;
    virtual void logDebugAppList() = 0;
    virtual bool goDebugHome() = 0;
};

bool RegisterDebugAppTarget(DebugAppTarget* target);
void UnregisterDebugAppTarget(DebugAppTarget* target);
DebugAppOpenResult RequestDebugAppOpen(std::string_view name);
bool RequestDebugAppList();
bool RequestDebugAppHome();
