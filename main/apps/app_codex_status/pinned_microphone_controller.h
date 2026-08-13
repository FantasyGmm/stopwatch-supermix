#pragma once

#include <cstdint>

enum class PinnedMicrophoneState : uint8_t {
    Unavailable,
    Ready,
    Active,
    Error,
};

class PinnedMicrophoneVoice {
public:
    virtual ~PinnedMicrophoneVoice() = default;
    virtual bool start_continuous(uint32_t now_ms) = 0;
    virtual bool stop_continuous(uint32_t now_ms) = 0;
    virtual void cancel_continuous(uint32_t now_ms) = 0;
    virtual bool active() const = 0;
};

class PinnedMicrophoneController {
public:
    bool set_enabled(
        bool enabled,
        bool voice_ready,
        PinnedMicrophoneVoice& voice,
        uint32_t now_ms);
    void stop_for_safety(PinnedMicrophoneVoice& voice, uint32_t now_ms);
    void sync(
        bool voice_ready,
        bool voice_error,
        const PinnedMicrophoneVoice& voice);
    void page_changed(int page);
    bool enabled() const;
    bool blocks_button_voice() const;
    PinnedMicrophoneState state() const;

private:
    bool _enabled = false;
    PinnedMicrophoneState _state = PinnedMicrophoneState::Unavailable;
};
