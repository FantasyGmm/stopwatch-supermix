#pragma once

class TouchInputGate {
public:
    void beginFrame(bool pressed)
    {
        if (_release_seen) {
            _suppressing = false;
            _release_seen = false;
        }

        _suppressed_this_frame = _suppressing;
        if (_suppressing && !pressed) {
            _release_seen = true;
        }
    }

    void suppressUntilRelease()
    {
        _suppressing = true;
        _release_seen = false;
        _suppressed_this_frame = true;
    }

    bool suppressedThisFrame() const
    {
        return _suppressed_this_frame;
    }

    void reset()
    {
        _suppressing = false;
        _release_seen = false;
        _suppressed_this_frame = false;
    }

private:
    bool _suppressing = false;
    bool _release_seen = false;
    bool _suppressed_this_frame = false;
};
