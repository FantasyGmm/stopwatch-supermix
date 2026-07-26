#include "hal/button_input_gate.h"

#include <cassert>

int main()
{
    ButtonInputGate gate;
    assert(gate.filter({true, false}).a_pressed);
    gate.suppressUntilRelease();
    assert(!gate.filter({true, false}).a_pressed);
    assert(gate.suppressing());
    assert(!gate.filter({false, true}).b_pressed);
    assert(gate.suppressing());
    const auto released = gate.filter({false, false});
    assert(!released.a_pressed && !released.b_pressed);
    assert(!gate.suppressing());
    assert(gate.filter({false, true}).b_pressed);
    return 0;
}
