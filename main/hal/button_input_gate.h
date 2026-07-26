#pragma once

struct ButtonPhysicalState {
    bool a_pressed = false;
    bool b_pressed = false;
};

class ButtonInputGate {
public:
    void suppressUntilRelease()
    {
        _suppressing = true;
    }

    ButtonPhysicalState filter(ButtonPhysicalState raw)
    {
        if (!_suppressing) {
            return raw;
        }
        if (!raw.a_pressed && !raw.b_pressed) {
            _suppressing = false;
        }
        return {};
    }

    bool suppressing() const
    {
        return _suppressing;
    }

private:
    bool _suppressing = false;
};
