#include "apps/app_codex_status/trae_status_ui_layout.h"

#include <cassert>
#include <cstring>


int main()
{
    using namespace trae_status_ui;

    assert(kBrandIconX == 168 && kBrandIconY == 22);
    assert(kBrandX == 218 && kBrandY == 32);
    assert(kHealthX == 118 && kHealthY == 67 && kHealthWidth == 230);
    assert(kHealthLabelY == 68);
    assert(kHealthDotY == 72 && kHealthDotSize == 6);
    assert(kBluetoothDotX == 156 && kBluetoothTextX == 167);
    assert(kProcessDotX == 234 && kProcessTextX == 245);
    assert(kRobotX == 96 && kRobotY == 105);
    assert(kRobotWidth == 77 && kRobotHeight == 86);
    assert(kTelemetryX == 214);
    assert(kDividerX == 82 && kDividerY == 223 && kDividerWidth == 302);
    assert(kSummaryY == 240);
    assert(kPendingSummaryX == 175 && kPendingCountX == 222);
    assert(kRunningSummaryX == 244 && kRunningCountX == 291);
    assert(kDeckX == 108 && kDeckY == 268);
    assert(kDeckWidth == 250 && kDeckHeight == 120);
    assert(kCardWidth == 250 && kCardHeight == 98);
    assert(kCardRadius == 15);
    assert(kBackCardScale == 241 && kDeepCardScale == 225);
    assert(kCardTextX == 17 && kCardIndexX == 137);
    assert(kCardStateY == 14 && kCardProjectY == 35);
    assert(kCardTitleY == 59 && kCardDetailY == 78);
    assert(kDeckHintY == 390);
    assert(kDeckY + kDeckHeight < kPageIndicatorY);
    assert(kPageIndicatorY == 416 && kBatteryY == 438);

    assert(kBrandIconRenderY == 19);
    assert(
        brand_icon_visible_center_twice() >=
        2 * kBrandTextVisibleCenterY - 2
    );
    assert(
        brand_icon_visible_center_twice() <=
        2 * kBrandTextVisibleCenterY + 2
    );

    const HealthState disconnected = resolve_health_state(
        false,
        false,
        TraeProcessState::Live
    );
    assert(!disconnected.bluetooth_live);
    assert(disconnected.process == ProcessHealth::Unavailable);
    assert(std::strcmp(bluetooth_health_text(disconnected), "BT OFF") == 0);
    assert(
        std::strcmp(process_health_text(disconnected.process), "TRAE --") == 0
    );

    const HealthState stale = resolve_health_state(
        true,
        true,
        TraeProcessState::Live
    );
    assert(stale.bluetooth_live);
    assert(stale.process == ProcessHealth::Unavailable);
    assert(std::strcmp(bluetooth_health_text(stale), "BT LIVE") == 0);
    assert(std::strcmp(process_health_text(stale.process), "TRAE --") == 0);
    const HealthState process_live = resolve_health_state(
        true,
        false,
        TraeProcessState::Live
    );
    const HealthState process_off = resolve_health_state(
        true,
        false,
        TraeProcessState::Off
    );
    assert(
        process_live.process == ProcessHealth::Live &&
        std::strcmp(process_health_text(process_live.process), "TRAE LIVE") == 0
    );
    assert(
        process_off.process == ProcessHealth::Off &&
        std::strcmp(process_health_text(process_off.process), "TRAE OFF") == 0
    );
    assert(
        resolve_health_state(true, false, TraeProcessState::Unknown).process ==
        ProcessHealth::Unavailable
    );

    assert(
        resolve_empty_state(
            false,
            true,
            TraeProcessState::Off,
            1,
            1,
            true
        ) == EmptyState::WaitingForConnection
    );
    assert(
        std::strcmp(
            empty_state_text(EmptyState::WaitingForConnection),
            "等待连接"
        ) == 0
    );
    assert(
        resolve_empty_state(
            true,
            true,
            TraeProcessState::Off,
            1,
            1,
            true
        ) == EmptyState::StatusUnavailable
    );
    assert(
        std::strcmp(
            empty_state_text(EmptyState::StatusUnavailable),
            "状态不可用"
        ) == 0
    );
    assert(
        resolve_empty_state(
            true,
            false,
            TraeProcessState::Off,
            0,
            0,
            false
        ) == EmptyState::ProcessOff
    );
    assert(
        std::strcmp(empty_state_text(EmptyState::ProcessOff), "TRAE 未运行") ==
        0
    );
    assert(
        resolve_empty_state(
            true,
            false,
            TraeProcessState::Unknown,
            0,
            0,
            false
        ) == EmptyState::StatusUnavailable
    );
    assert(
        resolve_empty_state(
            true,
            false,
            TraeProcessState::Live,
            0,
            0,
            false
        ) == EmptyState::NoActiveTasks
    );
    assert(
        std::strcmp(
            empty_state_text(EmptyState::NoActiveTasks),
            "暂无进行中的任务"
        ) == 0
    );
    assert(
        resolve_empty_state(
            true,
            false,
            TraeProcessState::Live,
            1,
            0,
            false
        ) == EmptyState::StatusUnavailable
    );
    assert(
        resolve_empty_state(
            true,
            false,
            TraeProcessState::Live,
            1,
            0,
            true
        ) == EmptyState::None
    );
    assert(empty_state_text(EmptyState::None) == nullptr);

    const DeckLayers one_card = resolve_deck_layers(1);
    const DeckLayers two_cards = resolve_deck_layers(2);
    const DeckLayers three_cards = resolve_deck_layers(3);
    assert(!one_card.deep && !one_card.back && one_card.front);
    assert(!two_cards.deep && two_cards.back && two_cards.front);
    assert(three_cards.deep && three_cards.back && three_cards.front);

    const RobotActivation active = resolve_robot_activation(true);
    const RobotActivation inactive = resolve_robot_activation(false);
    assert(!active.paused && !active.action);
    assert(active.period_ms == kRobotIdleDurationMs);
    assert(inactive.paused && !inactive.action);
    assert(inactive.period_ms == kRobotIdleDurationMs);
    const RobotTimerStep from_idle = resolve_robot_timer_step(false);
    const RobotTimerStep from_action = resolve_robot_timer_step(true);
    assert(from_idle.action && from_idle.period_ms == kRobotActionDurationMs);
    assert(!from_action.action && from_action.period_ms == kRobotIdleDurationMs);
    assert(should_trigger_task_action(true, "task-a", "task-b"));
    assert(!should_trigger_task_action(true, "task-a", "task-a"));
    assert(!should_trigger_task_action(false, "task-a", "task-b"));
    assert(!should_trigger_task_action(true, "task-a", ""));
}
