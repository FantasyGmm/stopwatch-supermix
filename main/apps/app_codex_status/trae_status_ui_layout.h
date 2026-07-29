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
constexpr uint32_t kRunning = 0x49C9EE;
constexpr uint32_t kPending = 0xFF6248;
constexpr uint32_t kProcessLive = 0x1CE994;
constexpr uint32_t kPrimaryText = 0xF2F6F8;
constexpr uint32_t kSecondaryText = 0xB8C2CC;
constexpr uint32_t kDimText = 0x788391;
constexpr uint32_t kProjectText = 0x99B1FF;

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

constexpr int kSummaryLabelY = 249;
constexpr int kSummaryCountY = 266;
constexpr int kPendingAnchorX = 110;
constexpr int kRunningAnchorX = 284;
constexpr int kSummaryAnchorWidth = 72;

constexpr int kCurrentProjectX = 112;
constexpr int kCurrentProjectY = 299;
constexpr int kCurrentTaskX = 112;
constexpr int kCurrentTaskY = 329;
constexpr int kCurrentTextWidth = 242;
constexpr int kNextCaptionX = 109;
constexpr int kNextTaskX = 165;
constexpr int kNextTaskY = 365;
constexpr int kNextCaptionWidth = 56;
constexpr int kNextTaskWidth = 192;
constexpr int kTaskFocusX = 82;
constexpr int kTaskFocusY = 241;
constexpr int kTaskFocusWidth = 302;
constexpr int kTaskFocusHeight = 165;

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
