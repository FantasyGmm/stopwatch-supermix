#include "button_feedback_policy.h"

bool ShouldPlayButtonSfx(bool configured, RuntimeOwner playback_owner)
{
    return configured && playback_owner == RuntimeOwner::None;
}
