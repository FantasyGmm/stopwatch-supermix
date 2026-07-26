#include "runtime/display/display_protection_logic.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>

int main()
{
    DisplayProtectionLogic logic;
    logic.reset(1000);
    assert(logic.state() == DisplayProtectionState::Active);
    assert(logic.update(15999).action == DisplayProtectionAction::None);
    assert(logic.update(16000).action == DisplayProtectionAction::Dim);
    assert(logic.update(60999).action == DisplayProtectionAction::None);
    assert(logic.update(61000).action == DisplayProtectionAction::ScreenOff);

    auto wake = logic.userActivity(62000);
    assert(wake.action == DisplayProtectionAction::Restore);
    assert(wake.consume_input);
    assert(logic.state() == DisplayProtectionState::Active);

    logic.reset(0);
    assert(logic.update(60000).action == DisplayProtectionAction::ScreenOff);
    logic.reset(UINT32_MAX - 10000U);
    assert(logic.update(4999U).action == DisplayProtectionAction::Dim);
    assert(logic.update(49999U).action == DisplayProtectionAction::ScreenOff);

    logic.reset(0);
    assert(logic.userActivity(14999).action == DisplayProtectionAction::None);
    assert(logic.update(29998).action == DisplayProtectionAction::None);
    assert(logic.update(29999).action == DisplayProtectionAction::Dim);
    auto dim_wake = logic.userActivity(30000);
    assert(dim_wake.action == DisplayProtectionAction::Restore);
    assert(!dim_wake.consume_input);

    logic.reset(0);
    assert(logic.setAutomaticEnabled(false, 100).action == DisplayProtectionAction::None);
    assert(!logic.automaticEnabled());
    assert(logic.update(70000).action == DisplayProtectionAction::None);
    logic.setAutomaticEnabled(true, 70000);
    assert(logic.update(84999).action == DisplayProtectionAction::None);
    assert(logic.update(85000).action == DisplayProtectionAction::Dim);
    auto disable_while_dimmed = logic.setAutomaticEnabled(false, 85001);
    assert(disable_while_dimmed.action == DisplayProtectionAction::Restore);

    logic.reset(0);
    logic.update(15000);
    auto held_while_dimmed = logic.setKeepAwake(
        DisplayKeepAwakeReason::AlarmRinging, true, 15001);
    assert(held_while_dimmed.action == DisplayProtectionAction::Restore);

    for (auto reason : {DisplayKeepAwakeReason::AssistantInteraction,
                        DisplayKeepAwakeReason::StopwatchRunning,
                        DisplayKeepAwakeReason::AlarmRinging}) {
        logic.reset(0);
        logic.setKeepAwake(reason, true, 100);
        logic.setKeepAwake(reason, true, 200);
        assert(logic.update(100000).action == DisplayProtectionAction::None);
        logic.setKeepAwake(reason, false, 100000);
        assert(logic.update(114999).action == DisplayProtectionAction::None);
        assert(logic.update(115000).action == DisplayProtectionAction::Dim);
    }

    logic.reset(0);
    logic.setKeepAwake(DisplayKeepAwakeReason::AssistantInteraction, true, 10);
    logic.setKeepAwake(DisplayKeepAwakeReason::StopwatchRunning, true, 20);
    logic.setKeepAwake(DisplayKeepAwakeReason::AssistantInteraction, false, 30);
    assert(logic.reasonMask() != 0);
    assert(logic.update(70000).action == DisplayProtectionAction::None);
    logic.setKeepAwake(DisplayKeepAwakeReason::StopwatchRunning, false, 70000);
    assert(logic.reasonMask() == 0);
    assert(DisplayProtectionLogic::dimBrightness(10) == 10);
    assert(DisplayProtectionLogic::dimBrightness(20) == 20);
    assert(DisplayProtectionLogic::dimBrightness(80) == 20);

    logic.reset(1000);
    logic.setTimeouts(30000U, 120000U, 1000U);
    const auto fresh_timing = logic.timingSnapshot(1000U);
    assert(fresh_timing.inactivity_ms == 0U);
    assert(fresh_timing.dim_after_ms == 30000U);
    assert(fresh_timing.screen_off_after_ms == 120000U);
    assert(fresh_timing.dim_remaining_ms == 30000U);
    assert(fresh_timing.screen_off_remaining_ms == 120000U);
    assert(logic.update(30999U).action == DisplayProtectionAction::None);
    assert(logic.update(31000U).action == DisplayProtectionAction::Dim);
    const auto dimmed_timing = logic.timingSnapshot(31000U);
    assert(dimmed_timing.dim_remaining_ms == 0U);
    assert(dimmed_timing.screen_off_remaining_ms == 90000U);
    assert(logic.update(120999U).action == DisplayProtectionAction::None);
    assert(logic.update(121000U).action == DisplayProtectionAction::ScreenOff);

    logic.reset(0);
    auto manual_off = logic.screenOffNow(100);
    assert(manual_off.action == DisplayProtectionAction::ScreenOff);
    assert(logic.state() == DisplayProtectionState::ScreenOff);
    auto manual_wake = logic.userActivity(101);
    assert(manual_wake.action == DisplayProtectionAction::Restore);
    assert(manual_wake.consume_input);

    logic.reset(0);
    logic.setKeepAwake(DisplayKeepAwakeReason::StopwatchRunning, true, 10);
    logic.screenOffNow(20);
    auto alarm_wake = logic.setKeepAwake(
        DisplayKeepAwakeReason::AlarmRinging, true, 30);
    assert(alarm_wake.action == DisplayProtectionAction::Restore);

    logic.reset(UINT32_MAX - 5000U);
    logic.setTimeouts(10000U, 30000U, UINT32_MAX - 5000U);
    assert(logic.update(4999U).action == DisplayProtectionAction::Dim);
    assert(logic.update(24999U).action == DisplayProtectionAction::ScreenOff);

    logic.reset(0);
    assert(logic.update(15000U).action == DisplayProtectionAction::Dim);
    const auto timeout_restore_from_dimmed =
        logic.setTimeouts(30000U, 120000U, 16000U);
    assert(timeout_restore_from_dimmed.action == DisplayProtectionAction::Restore);
    assert(logic.state() == DisplayProtectionState::Active);
    assert(logic.timingSnapshot(16000U).inactivity_ms == 0U);

    logic.screenOffNow(17000U);
    const auto timeout_restore_from_off =
        logic.setTimeouts(10000U, 30000U, 18000U);
    assert(timeout_restore_from_off.action == DisplayProtectionAction::Restore);
    assert(logic.state() == DisplayProtectionState::Active);

    logic.setTimeouts(UINT32_MAX, UINT32_MAX, 20000U);
    auto sanitized_timing = logic.timingSnapshot(20000U);
    assert(sanitized_timing.dim_after_ms == UINT32_MAX - 1U);
    assert(sanitized_timing.screen_off_after_ms == UINT32_MAX);
    logic.setTimeouts(UINT32_MAX, 0U, 21000U);
    sanitized_timing = logic.timingSnapshot(21000U);
    assert(sanitized_timing.dim_after_ms == UINT32_MAX - 1U);
    assert(sanitized_timing.screen_off_after_ms == UINT32_MAX);

    logic.reset(0);
    logic.setAutomaticEnabled(false, 1U);
    const auto disabled_manual_off = logic.screenOffNow(2U);
    assert(disabled_manual_off.action == DisplayProtectionAction::ScreenOff);
    assert(logic.state() == DisplayProtectionState::ScreenOff);
    return 0;
}
