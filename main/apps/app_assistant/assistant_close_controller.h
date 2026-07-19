#pragma once

#include <cstdint>

enum class AssistantCloseTrigger : uint8_t {
    None,
    PhysicalHome,
    DebugHome,
};

class AssistantCloseController {
public:
    bool request(AssistantCloseTrigger trigger);
    void reset();
    bool requested() const { return _trigger != AssistantCloseTrigger::None; }
    AssistantCloseTrigger trigger() const { return _trigger; }

private:
    AssistantCloseTrigger _trigger = AssistantCloseTrigger::None;
};
