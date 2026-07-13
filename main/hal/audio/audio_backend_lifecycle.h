/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

enum class AudioOpenResult { Opened, AlreadyOpen };
enum class AudioCloseResult { Closed, AlreadyClosed };

class AudioBackendLifecycle {
public:
    AudioOpenResult openRecorder();
    AudioCloseResult closeRecorder();
    bool recorderOpen() const;

    AudioOpenResult openPlayer();
    AudioCloseResult closePlayer();
    bool playerOpen() const;

private:
    bool _recorder_open = false;
    bool _player_open   = false;
};
