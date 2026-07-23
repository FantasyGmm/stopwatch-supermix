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
    WakeDetected,
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

    void reset()
    {
        _state = AssistantVisualState::Connecting;
        _wake_detected = false;
    }

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
                _wake_detected = false;
                break;
            case AssistantPresentationSignal::WakeDetected:
                if (_state == AssistantVisualState::WaitingWake) {
                    _wake_detected = true;
                    _state = AssistantVisualState::Listening;
                }
                break;
            case AssistantPresentationSignal::ListeningChanged:
                if (active && _wake_detected && _state == AssistantVisualState::WaitingWake) {
                    _state = AssistantVisualState::Listening;
                } else if (!active && _state == AssistantVisualState::Listening) {
                    _state = AssistantVisualState::WaitingWake;
                }
                break;
            case AssistantPresentationSignal::UserText:
                _state = AssistantVisualState::Thinking;
                break;
            case AssistantPresentationSignal::AgentText:
                _state = AssistantVisualState::Speaking;
                break;
            case AssistantPresentationSignal::SpeakingChanged:
                if (active) {
                    _state = AssistantVisualState::Speaking;
                } else if (_state == AssistantVisualState::Speaking) {
                    _state = AssistantVisualState::WaitingWake;
                }
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
    bool _wake_detected = false;
};
