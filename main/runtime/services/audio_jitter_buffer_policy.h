#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

class AudioJitterBufferPolicy {
public:
    AudioJitterBufferPolicy(std::size_t start_buffer_bytes, std::int64_t max_buffer_wait_us)
        : _start_buffer_bytes(start_buffer_bytes), _max_buffer_wait_us(max_buffer_wait_us)
    {
    }

    void onQueued(std::size_t bytes, std::int64_t now_us)
    {
        if (_queued_bytes == 0) {
            _buffer_started_us = now_us;
        }
        _queued_bytes += bytes;
    }

    void onConsumed(std::size_t bytes)
    {
        _queued_bytes = bytes < _queued_bytes ? _queued_bytes - bytes : 0;
        if (_queued_bytes == 0) {
            _buffering = true;
            _buffer_started_us = 0;
        }
    }

    bool readyToPlay(std::int64_t now_us)
    {
        if (_queued_bytes == 0) {
            return false;
        }
        if (!_buffering) {
            return true;
        }
        if (_queued_bytes >= _start_buffer_bytes ||
            (_buffer_started_us > 0 && now_us - _buffer_started_us >= _max_buffer_wait_us)) {
            _buffering = false;
            return true;
        }
        return false;
    }

    std::size_t queuedBytes() const
    {
        return _queued_bytes;
    }

    void reset()
    {
        _queued_bytes = 0;
        _buffer_started_us = 0;
        _buffering = true;
    }

private:
    const std::size_t _start_buffer_bytes;
    const std::int64_t _max_buffer_wait_us;
    std::size_t _queued_bytes = 0;
    std::int64_t _buffer_started_us = 0;
    bool _buffering = true;
};
