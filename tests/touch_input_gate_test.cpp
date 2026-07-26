#include "runtime/display/touch_input_gate.h"

#include "apps/app_codex_status/status_page_swipe_logic.h"
#include "apps/common/edge_control/edge_control_logic.h"

#include <cassert>
#include <cstdint>

namespace {

struct RawTouchConsumers {
    EdgeControlLogic edge;
    StatusPageSwipeLogic page;
    bool last_pressed = false;
    bool edge_ever_activated = false;
    StatusPageSwipeResult page_result = StatusPageSwipeResult::Stay;

    void update(const TouchInputGate& gate, bool pressed, int x, int y,
                uint32_t now_ms)
    {
        if (gate.suppressedThisFrame()) {
            edge.reset();
            page.cancel();
            last_pressed = false;
            return;
        }

        if (pressed) {
            if (!last_pressed) {
                edge.press(x, y, 50, 60, now_ms);
                page.press(x, y, now_ms);
            }
            const auto edge_update = edge.move(x, y, now_ms);
            edge_ever_activated = edge_ever_activated || edge_update.activated;
            page.move(x, y);
        } else if (last_pressed) {
            edge.release(now_ms);
            page_result = page.release(now_ms, 0);
        }
        last_pressed = pressed;
    }
};

} // namespace

int main()
{
    TouchInputGate gate;
    RawTouchConsumers consumers;

    // The PR that wakes a dark display is consumed in the same frame.
    gate.beginFrame(true);
    gate.suppressUntilRelease();
    consumers.update(gate, true, 300, 300, 1000);

    // Continued PR frames and their first real REL are consumed globally.
    gate.beginFrame(true);
    consumers.update(gate, true, 300, 220, 1040);
    gate.beginFrame(true);
    consumers.update(gate, true, 250, 180, 1080);
    gate.beginFrame(false);
    consumers.update(gate, false, 250, 180, 1120);

    assert(!consumers.edge_ever_activated);
    assert(!consumers.edge.active());
    assert(consumers.page_result == StatusPageSwipeResult::Stay);
    assert(gate.suppressedThisFrame());

    // A new PR after the consumed REL is a normal gesture for every consumer.
    gate.beginFrame(true);
    consumers.update(gate, true, 80, 300, 2000);
    assert(!gate.suppressedThisFrame());
    gate.beginFrame(true);
    consumers.update(gate, true, 80, 220, 2040);
    gate.beginFrame(false);
    consumers.update(gate, false, 80, 220, 2100);

    assert(consumers.edge_ever_activated);

    gate.beginFrame(true);
    consumers.update(gate, true, 300, 220, 2200);
    gate.beginFrame(true);
    consumers.update(gate, true, 250, 220, 2240);
    gate.beginFrame(false);
    consumers.update(gate, false, 250, 220, 2300);
    assert(consumers.page_result == StatusPageSwipeResult::Trae);
    return 0;
}
