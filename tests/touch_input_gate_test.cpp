#include "hal/touch_input_gate.h"
#include "hal/touch_sample_queue.h"

#include "apps/app_codex_status/status_page_swipe_logic.h"
#include "apps/common/edge_control/edge_control_logic.h"

#include <cassert>
#include <cstdint>

namespace {

struct RawTouchConsumers {
    EdgeControlLogic edge;
    StatusPageSwipeLogic page;
    const EdgeControlRect* edge_exclusion = nullptr;
    bool last_pressed = false;
    bool edge_ever_activated = false;
    bool edge_owns_touch = false;
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
                edge.press(x, y, 50, 60, now_ms, edge_exclusion);
                page.press(x, y, now_ms);
            }
            const auto edge_update = edge.move(x, y, now_ms);
            edge_ever_activated = edge_ever_activated || edge_update.activated;
            if (edge_update.activated) {
                edge_owns_touch = true;
                page.cancel();
            } else if (!edge_owns_touch) {
                page.move(x, y);
            }
        } else if (last_pressed) {
            const EdgeControlUpdate edge_release = edge.release(now_ms);
            page_result = edge_owns_touch || edge_release.consumed
                ? StatusPageSwipeResult::Stay
                : page.release(now_ms, 1);
            edge_owns_touch = false;
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

    // Production order is global edge first, then page. Codex reserves only
    // its bottom-center deck-entry rectangle, so the page sees the full swipe.
    const EdgeControlRect deck_exclusion = {183, 423, 283, 465};
    RawTouchConsumers codex_consumers;
    codex_consumers.edge_exclusion = &deck_exclusion;
    codex_consumers.update(gate, gate.filterForLvgl(true), 183, 465, 3000);
    codex_consumers.update(gate, gate.filterForLvgl(true), 183, 365, 3060);
    codex_consumers.update(gate, gate.filterForLvgl(false), 183, 365, 3100);
    assert(!codex_consumers.edge_ever_activated);
    assert(!codex_consumers.edge.active());
    assert(codex_consumers.page_result == StatusPageSwipeResult::OpenTaskDeck);

    // The adjacent point remains a normal global brightness gesture.
    RawTouchConsumers outside_consumers;
    outside_consumers.edge_exclusion = &deck_exclusion;
    outside_consumers.update(gate, gate.filterForLvgl(true), 182, 440, 3200);
    outside_consumers.update(gate, gate.filterForLvgl(true), 182, 390, 3240);
    outside_consumers.update(gate, gate.filterForLvgl(false), 182, 390, 3300);
    assert(outside_consumers.edge_ever_activated);

    // Once Codex clears the exclusion, the center gesture is globally owned.
    RawTouchConsumers other_app_consumers;
    other_app_consumers.update(gate, gate.filterForLvgl(true), 233, 440, 3400);
    other_app_consumers.update(gate, gate.filterForLvgl(true), 233, 390, 3440);
    other_app_consumers.update(gate, gate.filterForLvgl(false), 233, 390, 3500);
    assert(other_app_consumers.edge_ever_activated);
    assert(other_app_consumers.page_result == StatusPageSwipeResult::Stay);

    // A vertical gesture from the global left-edge region is not claimed by
    // page/deck gesture logic. Edge control remains its only possible owner.
    consumers.page_result = StatusPageSwipeResult::OpenTaskDeck;
    consumers.update(gate, gate.filterForLvgl(true), 10, 440, 2120);
    consumers.update(gate, gate.filterForLvgl(true), 10, 380, 2160);
    consumers.update(gate, gate.filterForLvgl(false), 10, 380, 2200);
    assert(consumers.page_result == StatusPageSwipeResult::Stay);

    consumers.update(gate, gate.filterForLvgl(true), 300, 220, 2300);
    consumers.update(gate, gate.filterForLvgl(true), 250, 220, 2340);
    consumers.update(gate, gate.filterForLvgl(false), 250, 220, 2400);
    assert(consumers.page_result == StatusPageSwipeResult::Usage);

    // A complete swipe sampled while LVGL is blocked still reaches the real
    // touch gate and page recognizer as Pressed, latest Move, Released. The
    // physical timestamps keep the valid swipe inside its duration limit.
    TouchSampleQueue buffered_swipe;
    buffered_swipe.push({true, 390, 220, 4000});
    buffered_swipe.push({true, 300, 220, 4040});
    buffered_swipe.push({true, 100, 220, 4120});
    buffered_swipe.push({false, -1, -1, 4140});
    RawTouchConsumers buffered_consumers;
    TouchSample buffered_sample;
    while (buffered_swipe.pop_for_lvgl(buffered_sample)) {
        buffered_consumers.update(
            gate,
            gate.filterForLvgl(buffered_sample.pressed),
            buffered_sample.x,
            buffered_sample.y,
            buffered_sample.timestamp_ms
        );
    }
    assert(buffered_consumers.page_result ==
           StatusPageSwipeResult::Usage);
    return 0;
}
