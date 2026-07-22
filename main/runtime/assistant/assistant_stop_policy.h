#pragma once

enum class AssistantStopPath {
    SubmitAgentStop,
    ReleasePendingSetup,
    WaitForOfflineAgentStop,
};

constexpr AssistantStopPath DecideAssistantStopPath(bool service_setup_pending,
                                                     bool wifi_connected)
{
    if (service_setup_pending) {
        return AssistantStopPath::ReleasePendingSetup;
    }
    if (!wifi_connected) {
        return AssistantStopPath::WaitForOfflineAgentStop;
    }
    return AssistantStopPath::SubmitAgentStop;
}
