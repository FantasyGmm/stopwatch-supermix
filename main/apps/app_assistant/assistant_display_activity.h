#pragma once

#include <cstdint>

enum class AssistantDisplayActivitySignal : uint8_t {
    WakeDetected,
    Listening,
    UserText,
    AgentText,
    Emotion,
    Speaking,
    Idle,
};

class AssistantDisplayActivity {
public:
    static constexpr uint32_t StaleAfterMs = 60000;

    void reset()
    {
        _listening = false;
        _speaking = false;
        _response_outstanding = false;
        _last_progress_ms = 0;
    }

    void observe(AssistantDisplayActivitySignal signal, bool active,
                 uint32_t now_ms)
    {
        switch (signal) {
            case AssistantDisplayActivitySignal::Listening:
                _listening = active;
                break;
            case AssistantDisplayActivitySignal::Speaking:
                _speaking = active;
                if (active) {
                    _response_outstanding = true;
                    _last_progress_ms = now_ms;
                } else {
                    _response_outstanding = false;
                }
                break;
            case AssistantDisplayActivitySignal::WakeDetected:
            case AssistantDisplayActivitySignal::UserText:
            case AssistantDisplayActivitySignal::AgentText:
            case AssistantDisplayActivitySignal::Emotion:
                if (active) {
                    _response_outstanding = true;
                    _last_progress_ms = now_ms;
                }
                break;
            case AssistantDisplayActivitySignal::Idle:
                reset();
                break;
        }
    }

    bool active(uint32_t now_ms) const
    {
        return _listening || _speaking ||
               (_response_outstanding &&
                static_cast<uint32_t>(now_ms - _last_progress_ms) < StaleAfterMs);
    }

private:
    bool _listening = false;
    bool _speaking = false;
    bool _response_outstanding = false;
    uint32_t _last_progress_ms = 0;
};
