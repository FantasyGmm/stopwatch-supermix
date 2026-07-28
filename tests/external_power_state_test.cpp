#include "hal/external_power_state.h"

#include <cassert>

int main()
{
    ExternalPowerTracker tracker;
    assert(tracker.snapshot().state == ExternalPowerState::Unknown);
    assert(!tracker.snapshot().latest_sample_valid);

    tracker.observe(false, false);
    assert(tracker.snapshot().state == ExternalPowerState::Unknown);

    tracker.observe(true, false);
    tracker.observe(true, false);
    auto boot_battery = tracker.observe(true, false);
    assert(boot_battery.changed);
    assert(boot_battery.snapshot.state == ExternalPowerState::Battery);

    auto inserted = tracker.observe(true, true);
    assert(inserted.changed);
    assert(inserted.snapshot.state == ExternalPowerState::ExternalPower);

    tracker.observe(true, false);
    tracker.observe(false, false);
    assert(!tracker.snapshot().latest_sample_valid);
    tracker.observe(true, false);
    tracker.observe(true, false);
    assert(tracker.snapshot().state == ExternalPowerState::ExternalPower);
    tracker.observe(true, true);
    assert(tracker.snapshot().state == ExternalPowerState::ExternalPower);

    tracker.observe(true, false);
    tracker.observe(true, false);
    auto removed = tracker.observe(true, false);
    assert(removed.changed);
    assert(removed.snapshot.state == ExternalPowerState::Battery);
    assert(removed.snapshot.revision == inserted.snapshot.revision + 1);
    return 0;
}
