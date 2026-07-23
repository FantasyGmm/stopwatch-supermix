#pragma once

enum class AssistantLifecycleEventKind {
    Activated,
    Started,
    Awake,
    Slept,
    Stopped,
    Other,
};

struct AssistantLifecycleEventDecision {
    bool publish_wake = false;
    bool report_unexpected = false;
};

constexpr AssistantLifecycleEventDecision EvaluateAssistantLifecycleEvent(
    AssistantLifecycleEventKind event, bool unexpected)
{
    const bool conversation_sync =
        event == AssistantLifecycleEventKind::Awake ||
        event == AssistantLifecycleEventKind::Slept;
    return {
        .publish_wake = event == AssistantLifecycleEventKind::Awake,
        .report_unexpected = unexpected && !conversation_sync,
    };
}
