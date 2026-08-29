#include "apps/app_codex_status/codex_screen_off_voice_policy.h"
#include "runtime/display/display_button_wake_policy.h"

#include <cassert>

namespace {

void test_only_codex_right_press_can_be_deferred_while_screen_is_off()
{
    assert(shouldDeferRightButtonWake(true, false, true, true));
    assert(!shouldDeferRightButtonWake(false, false, true, true));
    assert(!shouldDeferRightButtonWake(true, false, true, false));
    assert(!shouldDeferRightButtonWake(true, true, true, true));
    assert(!shouldDeferRightButtonWake(true, false, false, true));
}

void test_short_screen_off_press_wakes_without_becoming_a_click()
{
    CodexScreenOffVoicePolicy policy;
    assert(policy.update(true, true, true, 100, 300) ==
           CodexScreenOffVoiceAction::None);
    assert(policy.update(true, false, true, 299, 300) ==
           CodexScreenOffVoiceAction::WakeDisplay);
}

void test_hold_reaches_voice_threshold_without_waking_display()
{
    CodexScreenOffVoicePolicy policy;
    assert(policy.update(true, true, true, 100, 300) ==
           CodexScreenOffVoiceAction::None);
    assert(policy.update(true, true, true, 399, 300) ==
           CodexScreenOffVoiceAction::None);
    assert(policy.update(true, true, true, 400, 300) ==
           CodexScreenOffVoiceAction::AllowVoice);
    assert(policy.update(true, true, true, 450, 300) ==
           CodexScreenOffVoiceAction::None);
    assert(policy.update(true, false, true, 500, 300) ==
           CodexScreenOffVoiceAction::None);
}

void test_unusable_voice_link_falls_back_to_waking()
{
    CodexScreenOffVoicePolicy policy;
    assert(policy.update(true, true, true, 100, 300) ==
           CodexScreenOffVoiceAction::None);
    assert(policy.update(true, true, false, 150, 300) ==
           CodexScreenOffVoiceAction::WakeDisplay);
}

void test_screen_wake_or_lifecycle_reset_discards_pending_hold()
{
    CodexScreenOffVoicePolicy policy;
    assert(policy.update(true, true, true, 100, 300) ==
           CodexScreenOffVoiceAction::None);
    assert(policy.update(false, true, true, 200, 300) ==
           CodexScreenOffVoiceAction::None);
    assert(policy.update(true, false, true, 250, 300) ==
           CodexScreenOffVoiceAction::None);

    assert(policy.update(true, true, true, 300, 300) ==
           CodexScreenOffVoiceAction::None);
    policy.reset();
    assert(policy.update(true, false, true, 350, 300) ==
           CodexScreenOffVoiceAction::None);
}

}  // namespace

int main()
{
    test_only_codex_right_press_can_be_deferred_while_screen_is_off();
    test_short_screen_off_press_wakes_without_becoming_a_click();
    test_hold_reaches_voice_threshold_without_waking_display();
    test_unusable_voice_link_falls_back_to_waking();
    test_screen_wake_or_lifecycle_reset_discards_pending_hold();
    return 0;
}
