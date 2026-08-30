#include "main/apps/app_codex_status/pinned_microphone_controller.h"
#include "main/apps/app_codex_status/pinned_microphone_meter.h"

#include <cassert>
#include <cstring>

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
        if (deactivate_on_stop) {
            active_value = false;
        }
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
    bool deactivate_on_stop = true;
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

void test_disable_publishes_stopping_until_voice_finishes()
{
    FakeVoice voice;
    voice.deactivate_on_stop = false;
    PinnedMicrophoneController controller;

    assert(controller.set_enabled(true, true, voice, 60));
    assert(controller.state() == PinnedMicrophoneState::Starting);
    controller.sync(true, false, voice);
    assert(controller.state() == PinnedMicrophoneState::Active);

    assert(controller.set_enabled(false, true, voice, 61));
    assert(!controller.enabled());
    assert(controller.state() == PinnedMicrophoneState::Stopping);

    controller.sync(true, false, voice);
    assert(controller.state() == PinnedMicrophoneState::Stopping);

    voice.active_value = false;
    controller.sync(true, false, voice);
    assert(controller.state() == PinnedMicrophoneState::Ready);
}

void test_status_text_distinguishes_bluetooth_from_voice_readiness()
{
    const PinnedMicrophoneStatusText disconnected =
        pinned_microphone_status_text(
            false, PinnedMicrophoneState::Unavailable);
    assert(std::strcmp(disconnected.connection, "等待 Mac 连接") == 0);
    assert(std::strcmp(disconnected.prompt, "等待 Mac 连接") == 0);

    const PinnedMicrophoneStatusText voice_unavailable =
        pinned_microphone_status_text(
            true, PinnedMicrophoneState::Unavailable);
    assert(std::strcmp(voice_unavailable.connection, "已连接到 Mac") == 0);
    assert(std::strcmp(voice_unavailable.prompt, "语音暂不可用") == 0);

    const PinnedMicrophoneStatusText voice_error =
        pinned_microphone_status_text(true, PinnedMicrophoneState::Error);
    assert(std::strcmp(voice_error.connection, "已连接到 Mac") == 0);
    assert(std::strcmp(voice_error.prompt, "语音异常 · 轻触重试") == 0);

    const PinnedMicrophoneStatusText active =
        pinned_microphone_status_text(true, PinnedMicrophoneState::Active);
    assert(std::strcmp(active.prompt, "收音中 · 轻触关闭") == 0);
}

void test_meter_is_hidden_when_inactive_and_uses_only_real_recent_levels()
{
    PinnedMicrophoneInputLevels levels{};
    levels[0] = 8192;
    levels[11] = 0;
    levels[12] = 1024;
    levels[13] = 4096;
    levels[14] = 16384;

    const auto inactive = pinned_microphone_meter_frame(false, levels);
    assert(!inactive.visible);
    assert((inactive.heights == std::array<uint8_t, 4>{0, 0, 0, 0}));

    const auto active = pinned_microphone_meter_frame(true, levels);
    assert(active.visible);
    assert((active.heights == std::array<uint8_t, 4>{12, 18, 38, 64}));
}

void test_meter_clamps_loud_input_and_keeps_silence_visible()
{
    PinnedMicrophoneInputLevels levels{};
    levels[11] = 65535;
    const auto frame = pinned_microphone_meter_frame(true, levels);
    assert((frame.heights == std::array<uint8_t, 4>{64, 12, 12, 12}));
}

}  // namespace

int main()
{
    test_enable_disable_and_duplicate_taps_are_idempotent();
    test_unready_or_failed_start_does_not_latch();
    test_page_navigation_preserves_but_safety_boundary_clears();
    test_runtime_loss_clears_stale_latch();
    test_enable_publishes_starting_until_voice_becomes_active();
    test_disable_publishes_stopping_until_voice_finishes();
    test_status_text_distinguishes_bluetooth_from_voice_readiness();
    test_meter_is_hidden_when_inactive_and_uses_only_real_recent_levels();
    test_meter_clamps_loud_input_and_keeps_silence_visible();
}
