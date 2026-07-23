#include "runtime/assistant/assistant_lifecycle_event_policy.h"

#include <cassert>

int main()
{
    const auto normal_awake =
        EvaluateAssistantLifecycleEvent(AssistantLifecycleEventKind::Awake, false);
    assert(normal_awake.publish_wake);
    assert(!normal_awake.report_unexpected);

    const auto recovered_awake =
        EvaluateAssistantLifecycleEvent(AssistantLifecycleEventKind::Awake, true);
    assert(recovered_awake.publish_wake);
    assert(!recovered_awake.report_unexpected);

    const auto recovered_sleep =
        EvaluateAssistantLifecycleEvent(AssistantLifecycleEventKind::Slept, true);
    assert(!recovered_sleep.publish_wake);
    assert(!recovered_sleep.report_unexpected);

    const auto unexpected_started =
        EvaluateAssistantLifecycleEvent(AssistantLifecycleEventKind::Started, true);
    assert(!unexpected_started.publish_wake);
    assert(unexpected_started.report_unexpected);

    const auto unexpected_stopped =
        EvaluateAssistantLifecycleEvent(AssistantLifecycleEventKind::Stopped, true);
    assert(!unexpected_stopped.publish_wake);
    assert(unexpected_stopped.report_unexpected);
    return 0;
}
