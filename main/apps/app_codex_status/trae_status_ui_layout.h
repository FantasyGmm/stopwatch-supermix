#pragma once

#include "trae_status_model.h"

#include <cstddef>
#include <cstdint>
#include <cstring>


namespace trae_status_ui {

constexpr int kScreenSize = 466;
constexpr int kPageIndicatorY = 416;
constexpr int kBatteryY = 438;

constexpr uint32_t kBackground = 0x000000;
constexpr uint32_t kCardBackground = 0x0B0D10;
constexpr uint32_t kAccent = 0x7C9DFF;
constexpr uint32_t kRunning = 0x49C9EE;
constexpr uint32_t kPending = 0xFF6248;
constexpr uint32_t kProcessLive = 0x1CE994;
constexpr uint32_t kPrimaryText = 0xF2F6F8;
constexpr uint32_t kSecondaryText = 0xB8C2CC;
constexpr uint32_t kDimText = 0x788391;
constexpr uint32_t kProjectText = 0x99B1FF;
constexpr uint32_t kCardTitleText = 0xD9E0E6;
constexpr uint32_t kHintText = 0x64707D;
constexpr uint32_t kPendingBorder = 0x7A3E35;
constexpr uint32_t kRunningBorder = 0x285B6B;

constexpr int kBrandIconX = 168;
constexpr int kBrandIconY = 22;
constexpr int kBrandIconScaleX = 77;
constexpr int kBrandIconScaleY = 112;
constexpr int kBrandIconVisibleTop = 23;
constexpr int kBrandIconVisibleBottom = 96;
constexpr int kBrandIconRenderYOffset = -3;
constexpr int kBrandIconRenderY = kBrandIconY + kBrandIconRenderYOffset;
constexpr int kBrandX = 218;
constexpr int kBrandY = 32;
constexpr int kBrandTextVisibleCenterY = 45;

constexpr int kHealthX = 118;
constexpr int kHealthY = 67;
constexpr int kHealthWidth = 230;
constexpr int kHealthLabelY = 68;
constexpr int kHealthDotY = 72;
constexpr int kHealthDotSize = 6;
constexpr int kBluetoothDotX = 156;
constexpr int kBluetoothTextX = 167;
constexpr int kBluetoothWidth = 60;
constexpr int kProcessDotX = 234;
constexpr int kProcessTextX = 245;
constexpr int kProcessWidth = 82;

constexpr int kRobotX = 96;
constexpr int kRobotY = 105;
constexpr int kRobotWidth = 77;
constexpr int kRobotHeight = 86;
constexpr int kRobotScale = 183;
constexpr int kTelemetryX = 214;
constexpr int kTelemetryWidth = 200;
constexpr int kModelCaptionY = 98;
constexpr int kModelValueY = 120;
constexpr int kEffortCaptionY = 154;
constexpr int kEffortValueY = 176;

constexpr int kDividerX = 82;
constexpr int kDividerY = 223;
constexpr int kDividerWidth = 302;

constexpr int kSummaryY = 240;
constexpr int kPendingSummaryX = 175;
constexpr int kPendingCountX = 222;
constexpr int kRunningSummaryX = 244;
constexpr int kRunningCountX = 291;

constexpr int kDeckX = 108;
constexpr int kDeckY = 268;
constexpr int kDeckWidth = 250;
constexpr int kDeckHeight = 120;
constexpr int kCardWidth = 250;
constexpr int kCardHeight = 98;
constexpr int kCardRadius = 15;
constexpr int kBackCardOffset = 10;
constexpr int kDeepCardOffset = 18;
constexpr int kBackCardScale = 241;
constexpr int kDeepCardScale = 225;
constexpr int kCardTextX = 17;
constexpr int kCardIndexX = 137;
constexpr int kCardTextWidth = 216;
constexpr int kCardIndexWidth = 96;
constexpr int kCardStateY = 14;
constexpr int kCardProjectY = 35;
constexpr int kCardTitleY = 59;
constexpr int kCardDetailY = 78;
constexpr int kDeckHintY = 390;

constexpr uint32_t kRobotIdleDurationMs = 1300;
constexpr uint32_t kRobotActionDurationMs = 320;


enum class ProcessHealth {
    Unavailable,
    Live,
    Off,
};


struct HealthState {
    bool bluetooth_live;
    ProcessHealth process;
};


enum class EmptyState {
    None,
    WaitingForConnection,
    StatusUnavailable,
    ProcessOff,
    NoActiveTasks,
};


struct DeckLayers {
    bool deep;
    bool back;
    bool front;
};


struct RobotActivation {
    bool paused;
    bool action;
    uint32_t period_ms;
};


struct RobotTimerStep {
    bool action;
    uint32_t period_ms;
};


constexpr HealthState resolve_health_state(
    bool bluetooth_connected,
    bool stale,
    TraeProcessState process_state
)
{
    if (!bluetooth_connected || stale) {
        return {bluetooth_connected, ProcessHealth::Unavailable};
    }
    if (process_state == TraeProcessState::Live) {
        return {true, ProcessHealth::Live};
    }
    if (process_state == TraeProcessState::Off) {
        return {true, ProcessHealth::Off};
    }
    return {true, ProcessHealth::Unavailable};
}


constexpr const char* process_health_text(ProcessHealth health)
{
    switch (health) {
        case ProcessHealth::Live:
            return "TRAE LIVE";
        case ProcessHealth::Off:
            return "TRAE OFF";
        case ProcessHealth::Unavailable:
            return "TRAE --";
    }
    return "TRAE --";
}


constexpr const char* bluetooth_health_text(const HealthState& health)
{
    return health.bluetooth_live ? "BT LIVE" : "BT OFF";
}


constexpr EmptyState resolve_empty_state(
    bool bluetooth_connected,
    bool stale,
    TraeProcessState process_state,
    uint16_t pending_count,
    uint16_t running_count,
    bool has_active_task
)
{
    if (!bluetooth_connected) {
        return EmptyState::WaitingForConnection;
    }
    if (stale) {
        return EmptyState::StatusUnavailable;
    }
    if (process_state == TraeProcessState::Off) {
        return EmptyState::ProcessOff;
    }
    if (process_state == TraeProcessState::Unknown) {
        return EmptyState::StatusUnavailable;
    }
    if (pending_count == 0 && running_count == 0) {
        return EmptyState::NoActiveTasks;
    }
    if (!has_active_task) {
        return EmptyState::StatusUnavailable;
    }
    return EmptyState::None;
}


constexpr const char* empty_state_text(EmptyState state)
{
    switch (state) {
        case EmptyState::WaitingForConnection:
            return "等待连接";
        case EmptyState::StatusUnavailable:
            return "状态不可用";
        case EmptyState::ProcessOff:
            return "TRAE 未运行";
        case EmptyState::NoActiveTasks:
            return "暂无进行中的任务";
        case EmptyState::None:
            return nullptr;
    }
    return nullptr;
}


constexpr DeckLayers resolve_deck_layers(size_t visible_task_count)
{
    return {
        visible_task_count >= 3,
        visible_task_count >= 2,
        true,
    };
}


constexpr RobotActivation resolve_robot_activation(bool active)
{
    return {!active, false, kRobotIdleDurationMs};
}


constexpr RobotTimerStep resolve_robot_timer_step(bool current_action)
{
    return current_action
               ? RobotTimerStep{false, kRobotIdleDurationMs}
               : RobotTimerStep{true, kRobotActionDurationMs};
}


inline bool should_trigger_task_action(
    bool active,
    const char* previous_id,
    const char* current_id
)
{
    return active && current_id != nullptr && current_id[0] != '\0' &&
           (previous_id == nullptr || std::strcmp(previous_id, current_id) != 0);
}


constexpr int brand_icon_visible_center_twice()
{
    return 2 * kBrandIconRenderY +
           ((kBrandIconVisibleTop + kBrandIconVisibleBottom) *
            kBrandIconScaleY) /
               256;
}

}  // namespace trae_status_ui
