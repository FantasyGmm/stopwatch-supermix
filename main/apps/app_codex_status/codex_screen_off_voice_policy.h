#pragma once

#include <cstdint>

enum class CodexScreenOffVoiceAction : uint8_t {
    None,
    WakeDisplay,
    AllowVoice,
};

class CodexScreenOffVoicePolicy {
public:
    void reset()
    {
        _state = State::Idle;
        _pressed_at_ms = 0;
    }

    CodexScreenOffVoiceAction update(
        bool screen_off,
        bool right_pressed,
        bool voice_available,
        uint32_t now_ms,
        uint32_t voice_hold_ms)
    {
        if (!screen_off) {
            reset();
            return CodexScreenOffVoiceAction::None;
        }

        switch (_state) {
        case State::Idle:
            if (!right_pressed) {
                return CodexScreenOffVoiceAction::None;
            }
            if (!voice_available) {
                return CodexScreenOffVoiceAction::WakeDisplay;
            }
            _state = State::Pending;
            _pressed_at_ms = now_ms;
            return CodexScreenOffVoiceAction::None;

        case State::Pending:
            if (!voice_available || !right_pressed) {
                reset();
                return CodexScreenOffVoiceAction::WakeDisplay;
            }
            if (now_ms - _pressed_at_ms < voice_hold_ms) {
                return CodexScreenOffVoiceAction::None;
            }
            _state = State::VoiceCommitted;
            return CodexScreenOffVoiceAction::AllowVoice;

        case State::VoiceCommitted:
            if (!right_pressed) {
                reset();
            }
            return CodexScreenOffVoiceAction::None;
        }

        reset();
        return CodexScreenOffVoiceAction::None;
    }

private:
    enum class State : uint8_t {
        Idle,
        Pending,
        VoiceCommitted,
    };

    State _state = State::Idle;
    uint32_t _pressed_at_ms = 0;
};
