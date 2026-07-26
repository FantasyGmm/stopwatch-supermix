#include <cassert>
#include <cstdint>

#include "runtime/power/low_battery_alert_logic.h"

LowBatteryAlertInput sample(uint32_t now_ms, uint8_t percent,
                            bool valid = true, bool external_power = false,
                            bool display_visible = true,
                            bool playback_available = true,
                            bool speaker_audible = true)
{
    return {now_ms, true, valid, percent, external_power, display_visible,
            playback_available, speaker_audible};
}

LowBatteryAlertInput tick(uint32_t now_ms, bool display_visible = true,
                          bool playback_available = true,
                          bool speaker_audible = true)
{
    return {now_ms, false, true, 0, false, display_visible,
            playback_available, speaker_audible};
}

int main()
{
    LowBatteryAlertLogic logic;
    assert(!logic.update(sample(0, 0, false)).play_sound);
    assert(!logic.update(sample(1000, 29)).play_sound);
    auto entered = logic.update(sample(2000, 28));
    assert(entered.play_sound && entered.show_text);
    assert(entered.battery_percent == 28);
    assert(!logic.update(sample(3000, 27)).play_sound);

    logic.update(sample(4000, 35));
    assert(logic.phase() == LowBatteryAlertPhase::Armed);
    assert(!logic.update(sample(5000, 29)).play_sound);
    assert(logic.update(sample(6000, 29)).play_sound);

    {
        LowBatteryAlertLogic policy;
        policy.update(sample(0, 29));
        policy.update(sample(1000, 31));
        assert(!policy.update(sample(2000, 29)).play_sound);
        assert(policy.update(sample(3000, 29)).play_sound);
        assert(!policy.update(sample(4000, 34)).play_sound);
    }

    {
        LowBatteryAlertLogic policy;
        policy.update(sample(0, 28));
        assert(policy.update(sample(1000, 28)).play_sound);
        auto charging = policy.update(sample(2000, 28, true, true));
        assert(charging.hide_text);
        assert(policy.phase() == LowBatteryAlertPhase::Armed);
        assert(!policy.update(sample(3000, 28)).play_sound);
        assert(policy.update(sample(4000, 28)).play_sound);
    }

    {
        LowBatteryAlertLogic policy;
        policy.update(sample(0, 28, true, false, true, false));
        auto entered = policy.update(sample(1000, 28, true, false, true, false));
        assert(entered.show_text && !entered.play_sound);
        auto charging = policy.update({1500, false, true, 0, true, true, false, true});
        assert(charging.hide_text);
        assert(!charging.show_text && !charging.play_sound);
        assert(policy.phase() == LowBatteryAlertPhase::Armed);
    }

    {
        LowBatteryAlertLogic policy;
        policy.update(sample(0, 29, true, false, false));
        auto off = policy.update(sample(1000, 29, true, false, false));
        assert(off.play_sound && !off.show_text);
        assert(policy.phase() == LowBatteryAlertPhase::LowPending);
        auto wake = policy.update(tick(2000, true));
        assert(wake.show_text && !wake.play_sound);
        assert(!policy.update(tick(5999)).hide_text);
        assert(policy.update(tick(6000)).hide_text);
    }

    {
        LowBatteryAlertLogic policy;
        policy.update(sample(0, 29, true, false, true, false));
        auto busy = policy.update(sample(1000, 29, true, false, true, false));
        assert(!busy.play_sound && busy.show_text);
        assert(policy.phase() == LowBatteryAlertPhase::LowPending);
        assert(policy.update(tick(1500, true, true)).play_sound);
        policy.deferSound();
        assert(policy.update(tick(1600, true, true)).play_sound);
    }

    {
        LowBatteryAlertLogic policy;
        policy.update(sample(0, 29, true, false, true, true, false));
        auto muted = policy.update(sample(1000, 29, true, false, true, true, false));
        assert(!muted.play_sound && muted.show_text);
        assert(policy.phase() == LowBatteryAlertPhase::LowAlerted);
    }

    {
        LowBatteryAlertLogic policy;
        policy.update(sample(0, 29));
        policy.update(sample(1000, 29));
        assert(policy.update(tick(1500, false)).hide_text);
        assert(policy.update(tick(9000, true)).show_text);
        assert(!policy.update(tick(12999, true)).hide_text);
        assert(policy.update(tick(13000, true)).hide_text);
    }

    {
        LowBatteryAlertLogic policy;
        const uint32_t near_wrap = UINT32_MAX - 1000U;
        policy.update(sample(near_wrap - 1000U, 29));
        policy.update(sample(near_wrap, 29));
        assert(!policy.update(tick(2998U)).hide_text);
        assert(policy.update(tick(2999U)).hide_text);
    }
}
