#include "hal/touch_input_gate.h"

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

struct LvglSliderProbe {
    int value_changed_count = 0;

    void dispatch(bool pressed)
    {
        if (pressed) {
            value_changed_count++;
        }
    }
};

} // namespace

int main()
{
    TouchInputGate gate;
    RawTouchConsumers consumers;
    LvglSliderProbe slider;

    // A physical PR while the display is off is swallowed before LVGL can
    // dispatch it to a widget, while still producing exactly one wake request.
    gate.armForScreenOff();
    slider.dispatch(gate.filterForLvgl(true));
    assert(slider.value_changed_count == 0);
    assert(gate.consumeWakeRequest());
    assert(!gate.consumeWakeRequest());
    assert(gate.suppressing());

    // The held PR and its first REL also stay outside LVGL. Only a new PR after
    // release can reach the slider.
    slider.dispatch(gate.filterForLvgl(true));
    slider.dispatch(gate.filterForLvgl(false));
    assert(slider.value_changed_count == 0);
    assert(!gate.suppressing());
    slider.dispatch(gate.filterForLvgl(true));
    assert(slider.value_changed_count == 1);

    // A non-touch restore (for example power insertion) must keep the gate
    // closed until the first physical sample after brightness is restored.
    gate.armForScreenOff();
    gate.releaseAfterNextSample();
    slider.dispatch(gate.filterForLvgl(true));
    assert(slider.value_changed_count == 1);
    assert(gate.consumeWakeRequest());
    gate.releaseAfterNextSample();
    slider.dispatch(gate.filterForLvgl(true));
    slider.dispatch(gate.filterForLvgl(false));
    assert(slider.value_changed_count == 1);

    // With no contact at the restore boundary, the first REL opens the gate
    // and the next independent PR is delivered normally.
    gate.armForScreenOff();
    gate.releaseAfterNextSample();
    slider.dispatch(gate.filterForLvgl(false));
    slider.dispatch(gate.filterForLvgl(true));
    assert(slider.value_changed_count == 2);

    // Lifecycle preparation must never release a wake gesture that is still
    // physically held, including a PR that has not yet been sampled.
    gate.armForScreenOff();
    gate.prepareForActiveDisplay();
    slider.dispatch(gate.filterForLvgl(true));
    assert(slider.value_changed_count == 2);
    assert(gate.consumeWakeRequest());
    gate.prepareForActiveDisplay();
    slider.dispatch(gate.filterForLvgl(true));
    slider.dispatch(gate.filterForLvgl(false));
    assert(slider.value_changed_count == 2);

    gate.prepareForActiveDisplay();

    // The PR that wakes a dark display is consumed in the same frame.
    gate.armForScreenOff();
    consumers.update(gate, gate.filterForLvgl(true), 300, 300, 1000);
    assert(gate.consumeWakeRequest());

    // Continued PR frames and their first real REL are consumed globally.
    consumers.update(gate, gate.filterForLvgl(true), 300, 220, 1040);
    consumers.update(gate, gate.filterForLvgl(true), 250, 180, 1080);
    consumers.update(gate, gate.filterForLvgl(false), 250, 180, 1120);

    assert(!consumers.edge_ever_activated);
    assert(!consumers.edge.active());
    assert(consumers.page_result == StatusPageSwipeResult::Stay);
    assert(gate.suppressedThisFrame());

    // A new PR after the consumed REL is a normal gesture for every consumer.
    consumers.update(gate, gate.filterForLvgl(true), 80, 300, 2000);
    assert(!gate.suppressedThisFrame());
    consumers.update(gate, gate.filterForLvgl(true), 80, 220, 2040);
    consumers.update(gate, gate.filterForLvgl(false), 80, 220, 2100);

    assert(consumers.edge_ever_activated);

    consumers.update(gate, gate.filterForLvgl(true), 300, 220, 2200);
    consumers.update(gate, gate.filterForLvgl(true), 250, 220, 2240);
    consumers.update(gate, gate.filterForLvgl(false), 250, 220, 2300);
    assert(consumers.page_result == StatusPageSwipeResult::Trae);
    return 0;
}
