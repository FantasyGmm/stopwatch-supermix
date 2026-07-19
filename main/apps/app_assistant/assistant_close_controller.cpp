#include "assistant_close_controller.h"

bool AssistantCloseController::request(AssistantCloseTrigger trigger)
{
    if (trigger == AssistantCloseTrigger::None || requested()) {
        return false;
    }
    _trigger = trigger;
    return true;
}

void AssistantCloseController::reset()
{
    _trigger = AssistantCloseTrigger::None;
}
