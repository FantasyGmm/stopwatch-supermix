#include "hal/touch_sample_queue.h"

#include <cassert>
#include <cstdint>

namespace {

TouchSample pressed(int x, int y, uint32_t timestamp_ms)
{
    return {true, x, y, timestamp_ms};
}

TouchSample released(uint32_t timestamp_ms)
{
    return {false, -1, -1, timestamp_ms};
}

void assert_sample(
    const TouchSample& sample,
    bool is_pressed,
    int x,
    int y,
    uint32_t timestamp_ms)
{
    assert(sample.pressed == is_pressed);
    assert(sample.x == x);
    assert(sample.y == y);
    assert(sample.timestamp_ms == timestamp_ms);
}

}  // namespace

int main()
{
    // Idle sampling keeps the existing LVGL cadence; only active contact pays
    // for the 10 ms hardware cadence proven by the physical prototype.
    assert(touch_sampling_period_ms(false) == 33);
    assert(touch_sampling_period_ms(true) == 10);

    // A complete physical swipe captured while rendering is stalled is replayed
    // to polling consumers as start, latest movement, then release.
    TouchSampleQueue stalled;
    stalled.push(pressed(60, 220, 20));
    stalled.push(pressed(128, 220, 40));
    stalled.push(pressed(264, 220, 80));
    stalled.push(pressed(400, 220, 110));
    stalled.push(released(120));

    TouchSample sample;
    assert(stalled.pop_for_lvgl(sample));
    assert_sample(sample, true, 60, 220, 20);
    assert(stalled.pop_for_lvgl(sample));
    assert_sample(sample, true, 400, 220, 110);
    assert(stalled.pop_for_lvgl(sample));
    assert_sample(sample, false, -1, -1, 120);
    assert(!stalled.pop_for_lvgl(sample));

    // An ongoing hold delivers its initial press and newest available movement
    // without inventing a release; a later physical release remains observable.
    TouchSampleQueue ongoing;
    ongoing.push(pressed(90, 200, 1000));
    ongoing.push(pressed(130, 205, 1010));
    ongoing.push(pressed(180, 210, 1020));
    assert(ongoing.pop_for_lvgl(sample));
    assert_sample(sample, true, 90, 200, 1000);
    assert(ongoing.pop_for_lvgl(sample));
    assert_sample(sample, true, 180, 210, 1020);
    assert(!ongoing.pop_for_lvgl(sample));
    ongoing.push(released(1030));
    assert(ongoing.pop_for_lvgl(sample));
    assert_sample(sample, false, -1, -1, 1030);

    // Stationary samples do not create a backlog.
    TouchSampleQueue deduplicated;
    deduplicated.push(released(1));
    deduplicated.push(released(2));
    assert(deduplicated.pop_for_lvgl(sample));
    assert_sample(sample, false, -1, -1, 1);
    assert(!deduplicated.pop_for_lvgl(sample));
    deduplicated.push(pressed(200, 200, 3));
    deduplicated.push(pressed(200, 200, 4));
    assert(deduplicated.pop_for_lvgl(sample));
    assert_sample(sample, true, 200, 200, 3);
    assert(!deduplicated.pop_for_lvgl(sample));

    // Saturation may coalesce movement, but the terminal release must survive
    // so LVGL cannot remain logically pressed.
    TouchSampleQueue saturated;
    saturated.push(pressed(0, 100, 100));
    for (int index = 1;
         index < static_cast<int>(TouchSampleQueue::capacity());
         ++index) {
        saturated.push(pressed(index * 4, 100, 100 + index));
    }
    saturated.push(released(200));
    assert(saturated.pop_for_lvgl(sample));
    assert_sample(sample, true, 0, 100, 100);
    assert(saturated.pop_for_lvgl(sample));
    assert(sample.pressed);
    assert(sample.x > 0);
    assert(saturated.pop_for_lvgl(sample));
    assert_sample(sample, false, -1, -1, 200);
    assert(!saturated.pop_for_lvgl(sample));

    // Reset discards stale backlog and logical delivery state.
    TouchSampleQueue reset;
    reset.push(pressed(10, 10, 300));
    reset.push(pressed(100, 10, 310));
    assert(reset.pop_for_lvgl(sample));
    reset.clear();
    reset.push(pressed(20, 20, 400));
    assert(reset.pop_for_lvgl(sample));
    assert_sample(sample, true, 20, 20, 400);

    // The queue preserves wrap-around timestamps verbatim; consumers own
    // wrap-safe elapsed-time arithmetic.
    TouchSampleQueue wrapped;
    wrapped.push(pressed(30, 30, UINT32_MAX - 4));
    wrapped.push(pressed(300, 30, 2));
    wrapped.push(released(5));
    assert(wrapped.pop_for_lvgl(sample));
    assert_sample(sample, true, 30, 30, UINT32_MAX - 4);
    assert(wrapped.pop_for_lvgl(sample));
    assert_sample(sample, true, 300, 30, 2);
    assert(wrapped.pop_for_lvgl(sample));
    assert_sample(sample, false, -1, -1, 5);

    return 0;
}
