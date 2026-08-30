#include "pinned_microphone_controller.h"

PinnedMicrophoneStatusText pinned_microphone_status_text(
    bool bluetooth_connected,
    PinnedMicrophoneState state)
{
    if (!bluetooth_connected) {
        return {"等待 Mac 连接", "等待 Mac 连接"};
    }

    switch (state) {
        case PinnedMicrophoneState::Ready:
            return {"已连接到 Mac", "轻触开启"};
        case PinnedMicrophoneState::Starting:
            return {"已连接到 Mac", "正在启动音频…"};
        case PinnedMicrophoneState::Active:
            return {"已连接到 Mac", "收音中 · 轻触关闭"};
        case PinnedMicrophoneState::Stopping:
            return {"已连接到 Mac", "正在关闭…"};
        case PinnedMicrophoneState::Error:
            return {"已连接到 Mac", "语音异常 · 轻触重试"};
        case PinnedMicrophoneState::Unavailable:
            return {"已连接到 Mac", "语音暂不可用"};
    }
    return {"已连接到 Mac", "语音暂不可用"};
}

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
        _state = PinnedMicrophoneState::Starting;
        return true;
    }

    if (!voice.stop_continuous(now_ms)) {
        _state = voice_ready ? PinnedMicrophoneState::Ready
                             : PinnedMicrophoneState::Unavailable;
        _enabled = false;
        return false;
    }
    _enabled = false;
    _state = PinnedMicrophoneState::Stopping;
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
    if (_state == PinnedMicrophoneState::Stopping) {
        if (voice_error) {
            _state = PinnedMicrophoneState::Error;
        } else if (!voice_ready) {
            _state = PinnedMicrophoneState::Unavailable;
        } else if (!voice.active()) {
            _state = PinnedMicrophoneState::Ready;
        }
        return;
    }
    if (_enabled) {
        if (voice_error) {
            _enabled = false;
            _state = PinnedMicrophoneState::Error;
            return;
        }
        if (!voice_ready) {
            _enabled = false;
            _state = PinnedMicrophoneState::Unavailable;
            return;
        }
        if (voice.starting()) {
            _state = PinnedMicrophoneState::Starting;
            return;
        }
        if (voice.active()) {
            _state = PinnedMicrophoneState::Active;
            return;
        }
        _enabled = false;
    }
    _state = voice_error
                 ? PinnedMicrophoneState::Error
                 : (voice_ready ? PinnedMicrophoneState::Ready
                                : PinnedMicrophoneState::Unavailable);
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
