#include "apps/app_assistant/assistant_display_activity.h"

#include <cassert>

int main()
{
    AssistantDisplayActivity activity;
    activity.reset();
    assert(!activity.active(0));
    activity.observe(AssistantDisplayActivitySignal::WakeDetected, true, 100);
    assert(activity.active(100));
    activity.observe(AssistantDisplayActivitySignal::Listening, true, 200);
    assert(activity.active(100000));
    activity.observe(AssistantDisplayActivitySignal::Listening, false, 100000);
    activity.observe(AssistantDisplayActivitySignal::UserText, true, 100100);
    assert(activity.active(160099));
    assert(!activity.active(160100));
    activity.observe(AssistantDisplayActivitySignal::Speaking, true, 170000);
    assert(activity.active(500000));
    activity.observe(AssistantDisplayActivitySignal::Speaking, false, 500001);
    assert(!activity.active(500001));
    activity.observe(AssistantDisplayActivitySignal::AgentText, true, 600000);
    assert(activity.active(600000));
    activity.observe(AssistantDisplayActivitySignal::Idle, true, 600001);
    assert(!activity.active(600001));
    return 0;
}
