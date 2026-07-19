#include "apps/app_assistant/assistant_close_controller.h"

#include <cassert>

int main()
{
    AssistantCloseController close;
    assert(!close.requested());
    assert(close.request(AssistantCloseTrigger::PhysicalHome));
    assert(close.requested());
    assert(close.trigger() == AssistantCloseTrigger::PhysicalHome);
    assert(!close.request(AssistantCloseTrigger::DebugHome));
    assert(close.trigger() == AssistantCloseTrigger::PhysicalHome);

    close.reset();
    assert(!close.requested());
    assert(close.request(AssistantCloseTrigger::DebugHome));
    assert(close.trigger() == AssistantCloseTrigger::DebugHome);
    return 0;
}
