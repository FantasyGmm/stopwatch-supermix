#include "main/apps/app_codex_status/pinned_microphone_controller.h"

#include <cassert>

namespace {

class FakeVoice final : public PinnedMicrophoneVoice {
public:
    bool start_continuous(uint32_t) override
    {
        ++starts;
        if (!start_ok || active_value) {
            return false;
        }
        active_value = activate_on_start;
        return true;
    }

    bool stop_continuous(uint32_t) override
    {
        ++stops;
        if (!active_value) {
            return false;
        }
        active_value = false;
        return true;
    }

    void cancel_continuous(uint32_t) override
    {
        ++cancels;
        active_value = false;
    }

    bool starting() const override { return starting_value; }
    bool active() const override { return active_value; }

    bool start_ok = true;
    bool activate_on_start = true;
    bool starting_value = false;
    bool active_value = false;
    int starts = 0;
    int stops = 0;
    int cancels = 0;
};

void test_enable_disable_and_duplicate_taps_are_idempotent()
{
    FakeVoice voice;
    PinnedMicrophoneController controller;
    assert(controller.set_enabled(true, true, voice, 10));
    assert(controller.enabled());
    assert(controller.blocks_button_voice());
    assert(!controller.set_enabled(true, true, voice, 11));
    assert(voice.starts == 1);
    assert(controller.set_enabled(false, true, voice, 12));
    assert(!controller.enabled());
    assert(!controller.set_enabled(false, true, voice, 13));
    assert(voice.stops == 1);
}

void test_unready_or_failed_start_does_not_latch()
{
    FakeVoice voice;
    PinnedMicrophoneController controller;
    assert(!controller.set_enabled(true, false, voice, 20));
    assert(voice.starts == 0);
    voice.start_ok = false;
    assert(!controller.set_enabled(true, true, voice, 21));
    assert(!controller.enabled());
}

void test_page_navigation_preserves_but_safety_boundary_clears()
{
    FakeVoice voice;
    PinnedMicrophoneController controller;
    assert(controller.set_enabled(true, true, voice, 30));
    controller.page_changed(0);
    controller.page_changed(2);
    assert(controller.enabled());
    assert(voice.cancels == 0);
    controller.stop_for_safety(voice, 31);
    controller.stop_for_safety(voice, 32);
    assert(!controller.enabled());
    assert(voice.cancels == 1);
}

void test_runtime_loss_clears_stale_latch()
{
    FakeVoice voice;
    PinnedMicrophoneController controller;
    assert(controller.set_enabled(true, true, voice, 40));
    voice.active_value = false;
    controller.sync(true, false, voice);
    assert(!controller.enabled());
    assert(controller.state() == PinnedMicrophoneState::Ready);
    controller.sync(true, true, voice);
    assert(controller.state() == PinnedMicrophoneState::Error);
}

void test_enable_publishes_starting_until_voice_becomes_active()
{
    FakeVoice voice;
    voice.activate_on_start = false;
    voice.starting_value = true;
    PinnedMicrophoneController controller;

    assert(controller.set_enabled(true, true, voice, 50));
    assert(controller.enabled());
    assert(controller.state() == PinnedMicrophoneState::Starting);

    controller.sync(true, false, voice);
    assert(controller.enabled());
    assert(controller.state() == PinnedMicrophoneState::Starting);

    voice.active_value = true;
    voice.starting_value = false;
    controller.sync(true, false, voice);
    assert(controller.state() == PinnedMicrophoneState::Active);
}

}  // namespace

int main()
{
    test_enable_disable_and_duplicate_taps_are_idempotent();
    test_unready_or_failed_start_does_not_latch();
    test_page_navigation_preserves_but_safety_boundary_clears();
    test_runtime_loss_clears_stale_latch();
    test_enable_publishes_starting_until_voice_becomes_active();
}
