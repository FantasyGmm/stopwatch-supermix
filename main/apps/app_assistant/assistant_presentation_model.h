#pragma once

#include <cstdint>

enum class AssistantVisualState : std::uint8_t {
    Connecting,
    Activating,
    WaitingWake,
    Listening,
    Thinking,
    Speaking,
    Failed,
    Closing,
    StopTimedOut,
};

enum class AssistantPresentationSignal : std::uint8_t {
    Connecting,
    Activating,
    Running,
    ListeningChanged,
    UserText,
    AgentText,
    SpeakingChanged,
    Failed,
    Closing,
    StopTimedOut,
};

class AssistantPresentationModel {
public:
    AssistantVisualState state() const { return _state; }

    void reset() { _state = AssistantVisualState::Connecting; }

    void apply(AssistantPresentationSignal signal, bool active = true)
    {
        if (_state == AssistantVisualState::Failed) {
            if (signal == AssistantPresentationSignal::Connecting) {
                _state = AssistantVisualState::Connecting;
            } else if (signal == AssistantPresentationSignal::StopTimedOut) {
                _state = AssistantVisualState::StopTimedOut;
            }
            return;
        }
        if (_state == AssistantVisualState::Closing ||
            _state == AssistantVisualState::StopTimedOut) {
            if (signal == AssistantPresentationSignal::StopTimedOut) {
                _state = AssistantVisualState::StopTimedOut;
            }
            return;
        }

        switch (signal) {
            case AssistantPresentationSignal::Connecting:
                _state = AssistantVisualState::Connecting;
                break;
            case AssistantPresentationSignal::Activating:
                _state = AssistantVisualState::Activating;
                break;
            case AssistantPresentationSignal::Running:
                _state = AssistantVisualState::WaitingWake;
                break;
            case AssistantPresentationSignal::ListeningChanged:
                if (active) {
                    _state = AssistantVisualState::Listening;
                }
                break;
            case AssistantPresentationSignal::UserText:
                _state = AssistantVisualState::Thinking;
                break;
            case AssistantPresentationSignal::AgentText:
                _state = AssistantVisualState::Speaking;
                break;
            case AssistantPresentationSignal::SpeakingChanged:
                _state = active ? AssistantVisualState::Speaking
                                : AssistantVisualState::WaitingWake;
                break;
            case AssistantPresentationSignal::Failed:
                _state = AssistantVisualState::Failed;
                break;
            case AssistantPresentationSignal::Closing:
                _state = AssistantVisualState::Closing;
                break;
            case AssistantPresentationSignal::StopTimedOut:
                _state = AssistantVisualState::StopTimedOut;
                break;
        }
    }

private:
    AssistantVisualState _state = AssistantVisualState::Connecting;
};
