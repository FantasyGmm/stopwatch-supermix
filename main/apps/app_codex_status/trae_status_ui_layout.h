#pragma once

#include "trae_status_model.h"

#include <array>
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
constexpr int kConsoleX = 161;
constexpr int kConsoleY = 78;
constexpr int kConsoleWidth = 247;
constexpr int kHealthCaptionY = 78;
constexpr int kHealthValueY = 95;
constexpr int kHealthDotSize = 6;
constexpr int kAgentX = 161;
constexpr int kAgentWidth = 118;
constexpr int kVoiceX = 291;
constexpr int kVoiceWidth = 118;
constexpr int kStatusDotY = 99;
constexpr int kStatusTextInset = 11;

constexpr int kSpineX = 129;
constexpr int kSpineY = 116;
constexpr int kSpineWidth = 2;
constexpr int kSpineHeight = 246;
constexpr int kPendingCountX = 45;
constexpr int kPendingCountY = 274;
constexpr int kRunningCountX = 45;
constexpr int kRunningCountY = 322;
constexpr int kRailCountWidth = 69;
constexpr int kRailLabelOffsetY = 22;

constexpr int kTaskLabelX = 161;
constexpr int kTaskLabelY = 131;
constexpr int kTaskLabelWidth = 247;
constexpr int kTaskRowWidth = 247;
constexpr int kTaskProjectX = 161;
constexpr int kTaskTitleX = 161;
constexpr int kTaskMetaX = 161;
constexpr int kTaskModelMaxWidth = 164;
constexpr int kTaskMetaGap = 7;
constexpr int kMicroFontPx = 13;
constexpr int kChannelValueFontPx = 15;
constexpr int kTaskGroupFontPx = 14;
constexpr int kRailLabelFontPx = 14;
constexpr int kMultiProjectFontPx = 13;
constexpr int kProjectFontPx = 17;
constexpr int kMultiTaskTitleMinFontPx = 20;
constexpr int kMultiTaskTitleMaxFontPx = 24;
constexpr int kSingleTaskTitleMinFontPx = 20;
constexpr int kSingleTaskTitleMaxFontPx = 28;
constexpr int kCountFontPx = 26;
constexpr int kBrandFontPx = 29;
constexpr int kScale14To15 = 274;
constexpr int kScale14To17 = 311;
constexpr int kEffortBadgeHeight = 20;
constexpr int kEffortBadgePadX = 7;
constexpr int kSingleTitleMaxHeight = 62;
constexpr int kSingleMetaGap = 15;
constexpr int kTransformScaleNone = 256;

constexpr int font_scale_for_pixels(int base_font_px, int visual_font_px)
{
    return base_font_px <= 0
               ? kTransformScaleNone
               : (visual_font_px * kTransformScaleNone + base_font_px / 2) /
                     base_font_px;
}

constexpr int title_font_scale(
    int raw_text_width,
    int visual_width,
    int base_font_px,
    int min_font_px,
    int max_font_px
)
{
    const int min_scale = font_scale_for_pixels(base_font_px, min_font_px);
    const int max_scale = font_scale_for_pixels(base_font_px, max_font_px);
    if (raw_text_width <= 0 || visual_width <= 0) {
        return max_scale;
    }
    const int fit_scale =
        visual_width * kTransformScaleNone / raw_text_width;
    return fit_scale < min_scale
               ? min_scale
               : (fit_scale > max_scale ? max_scale : fit_scale);
}

constexpr bool title_overflows(
    int raw_text_width,
    int scale,
    int visual_width
)
{
    return raw_text_width > 0 &&
           raw_text_width * scale > visual_width * kTransformScaleNone;
}

constexpr int scaled_visual_height(int raw_height, int scale)
{
    const int scaled = raw_height * scale / kTransformScaleNone;
    return scaled < 1 ? 1 : scaled;
}

struct TaskRowGeometry {
    int top;
    int project_y;
    int title_y;
    int meta_y;
};

constexpr std::array<TaskRowGeometry, 2> kMultiTaskRows{{
    {138, 148, 164, 191},
    {213, 223, 239, 266},
}};
constexpr TaskRowGeometry kSingleTaskRow{145, 173, 201, 0};
constexpr int kTaskDividerY = 216;
constexpr int kRunningSummaryY = 295;
constexpr int kEmptyStateY = 205;

constexpr int kTaskFocusX = 161;
constexpr int kTaskFocusY = 127;
constexpr int kTaskFocusWidth = 247;
constexpr int kTaskFocusHeight = 236;

constexpr int single_task_meta_y(int title_visual_height)
{
    const int bounded_height = title_visual_height < 1
                                   ? 1
                                   : (title_visual_height > kSingleTitleMaxHeight
                                          ? kSingleTitleMaxHeight
                                          : title_visual_height);
    return kSingleTaskRow.title_y + bounded_height + kSingleMetaGap;
}

enum class TaskLayout {
    Empty,
    Single,
    Multiple,
};

constexpr TaskLayout resolve_task_layout(size_t active_task_count)
{
    return active_task_count == 0
               ? TaskLayout::Empty
               : (active_task_count == 1 ? TaskLayout::Single
                                         : TaskLayout::Multiple);
}

constexpr const char* task_group_label(TraeTaskState state)
{
    return state == TraeTaskState::Pending ? "PENDING TASKS" : "RUNNING TASKS";
}

constexpr uint32_t task_group_color(TraeTaskState state)
{
    return state == TraeTaskState::Pending ? kPending : kRunning;
}


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


constexpr int brand_icon_visible_center_twice()
{
    return 2 * kBrandIconRenderY +
           ((kBrandIconVisibleTop + kBrandIconVisibleBottom) *
            kBrandIconScaleY) /
               256;
}

}  // namespace trae_status_ui
