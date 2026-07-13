/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "audio_backend_lifecycle.h"

AudioOpenResult AudioBackendLifecycle::openRecorder()
{
    if (_recorder_open) {
        return AudioOpenResult::AlreadyOpen;
    }
    _recorder_open = true;
    return AudioOpenResult::Opened;
}

AudioCloseResult AudioBackendLifecycle::closeRecorder()
{
    if (!_recorder_open) {
        return AudioCloseResult::AlreadyClosed;
    }
    _recorder_open = false;
    return AudioCloseResult::Closed;
}

bool AudioBackendLifecycle::recorderOpen() const
{
    return _recorder_open;
}

AudioOpenResult AudioBackendLifecycle::openPlayer()
{
    if (_player_open) {
        return AudioOpenResult::AlreadyOpen;
    }
    _player_open = true;
    return AudioOpenResult::Opened;
}

AudioCloseResult AudioBackendLifecycle::closePlayer()
{
    if (!_player_open) {
        return AudioCloseResult::AlreadyClosed;
    }
    _player_open = false;
    return AudioCloseResult::Closed;
}

bool AudioBackendLifecycle::playerOpen() const
{
    return _player_open;
}
