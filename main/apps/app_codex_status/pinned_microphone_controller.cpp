#include "pinned_microphone_controller.h"

bool PinnedMicrophoneController::set_enabled(
    bool enabled,
    bool voice_ready,
    PinnedMicrophoneVoice& voice,
    uint32_t now_ms)
{
    if (enabled == _enabled) {
        return false;
    }
    if (enabled) {
        if (!voice_ready) {
            _state = PinnedMicrophoneState::Unavailable;
            return false;
        }
        if (!voice.start_continuous(now_ms)) {
            _state = PinnedMicrophoneState::Error;
            return false;
        }
        _enabled = true;
        _state = PinnedMicrophoneState::Active;
        return true;
    }

    if (!voice.stop_continuous(now_ms)) {
        _state = voice_ready ? PinnedMicrophoneState::Ready
                             : PinnedMicrophoneState::Unavailable;
        _enabled = false;
        return false;
    }
    _enabled = false;
    _state = voice_ready ? PinnedMicrophoneState::Ready
                         : PinnedMicrophoneState::Unavailable;
    return true;
}

void PinnedMicrophoneController::stop_for_safety(
    PinnedMicrophoneVoice& voice,
    uint32_t now_ms)
{
    if (!_enabled) {
        return;
    }
    voice.cancel_continuous(now_ms);
    _enabled = false;
    _state = PinnedMicrophoneState::Unavailable;
}

void PinnedMicrophoneController::sync(
    bool voice_ready,
    bool voice_error,
    const PinnedMicrophoneVoice& voice)
{
    if (_enabled && !voice.active()) {
        _enabled = false;
    }
    _state = _enabled
                 ? PinnedMicrophoneState::Active
                 : (voice_error
                        ? PinnedMicrophoneState::Error
                        : (voice_ready ? PinnedMicrophoneState::Ready
                                       : PinnedMicrophoneState::Unavailable));
}

void PinnedMicrophoneController::page_changed(int page)
{
    (void)page;
}

bool PinnedMicrophoneController::enabled() const
{
    return _enabled;
}

bool PinnedMicrophoneController::blocks_button_voice() const
{
    return _enabled;
}

PinnedMicrophoneState PinnedMicrophoneController::state() const
{
    return _state;
}
