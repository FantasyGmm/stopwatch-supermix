#pragma once

class DebugAssistantTarget {
public:
    virtual ~DebugAssistantTarget() = default;
    virtual bool requestDebugGracefulHome() = 0;
};

bool RegisterDebugAssistantTarget(DebugAssistantTarget* target);
void UnregisterDebugAssistantTarget(DebugAssistantTarget* target);
bool RequestDebugAssistantHome();
