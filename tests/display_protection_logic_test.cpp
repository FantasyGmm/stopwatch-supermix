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
    return 0;
}
