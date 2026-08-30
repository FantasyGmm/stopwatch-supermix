#include "apps/app_codex_status/trae_status_ui_layout.h"

#include <cassert>
#include <cstring>


int main()
{
    using namespace trae_status_ui;

    assert(kBrandIconX == 168 && kBrandIconY == 22);
    assert(kBrandX == 218 && kBrandY == 32);
    assert(kConsoleX == 161 && kConsoleY == 78 && kConsoleWidth == 247);
    assert(kHealthCaptionY == 78 && kHealthValueY == 95);
    assert(kAgentX == 161 && kAgentWidth == 118);
    assert(kVoiceX == 291 && kVoiceWidth == 118);
    assert(kHealthDotSize == 6);
    assert(kSpineX == 129 && kSpineY == 116);
    assert(kSpineWidth == 2 && kSpineHeight == 246);
    assert(kPendingCountX == 45 && kPendingCountY == 274);
    assert(kRunningCountX == 45 && kRunningCountY == 322);
    assert(kRailCountWidth == 69);
    assert(kRailLabelOffsetY == 22);
    assert(kPendingCountX + kRailCountWidth == 114);
    assert(kRunningCountX + kRailCountWidth == 114);
    assert(kPendingCountX + kRailCountWidth < kSpineX);
    assert(kTaskLabelX == 161 && kTaskLabelY == 131);
    assert(kTaskLabelWidth == 247);
    assert(kMultiTaskRows[0].top == 138);
    assert(kMultiTaskRows[0].project_y == 148);
    assert(kMultiTaskRows[0].title_y == 164);
    assert(kMultiTaskRows[0].meta_y == 191);
    assert(kMultiTaskRows[1].top == 213);
    assert(kMultiTaskRows[1].project_y == 223);
    assert(kMultiTaskRows[1].title_y == 239);
    assert(kMultiTaskRows[1].meta_y == 266);
    assert(kTaskDividerY == 216);
    assert(kSingleTaskRow.top == 145);
    assert(kSingleTaskRow.project_y == 173);
    assert(kSingleTaskRow.title_y == 201);
    assert(single_task_meta_y(29) == 245);
    assert(single_task_meta_y(58) == 274);
    assert(single_task_meta_y(100) == 278);
    assert(kTaskFocusX == 161 && kTaskFocusY == 127);
    assert(kTaskFocusWidth == 247 && kTaskFocusHeight == 236);
    assert(kPageIndicatorY == 416 && kBatteryY == 438);
    assert(kMicroFontPx == 13);
    assert(kChannelValueFontPx == 15);
    assert(kTaskGroupFontPx == 14);
    assert(kRailLabelFontPx == 14);
    assert(kMultiProjectFontPx == 13);
    assert(kProjectFontPx == 17);
    assert(kMultiTaskTitleMinFontPx == 20);
    assert(kMultiTaskTitleMaxFontPx == 24);
    assert(kSingleTaskTitleMinFontPx == 20);
    assert(kSingleTaskTitleMaxFontPx == 28);
    assert(kCountFontPx == 26);
    assert(kBrandFontPx == 29);
    assert(kEffortBadgeHeight == 20);
    assert(kScale14To15 == 274);
    assert(kScale14To17 == 311);
    assert(title_font_scale(180, 247, 21, 20, 28) == 341);
    assert(title_font_scale(190, 247, 21, 20, 28) == 332);
    assert(!title_overflows(190, 332, 247));
    assert(title_font_scale(300, 247, 21, 20, 28) == 244);
    assert(title_overflows(300, 244, 247));
    assert(scaled_visual_height(26, 256) == 26);
    assert(scaled_visual_height(16, 384) == 24);
    assert(
        kMultiTaskRows[0].title_y >=
        kMultiTaskRows[0].project_y + kMultiProjectFontPx
    );
    assert(
        kMultiTaskRows[1].title_y >=
        kMultiTaskRows[1].project_y + kMultiProjectFontPx
    );

    assert(resolve_task_layout(0) == TaskLayout::Empty);
    assert(resolve_task_layout(1) == TaskLayout::Single);
    assert(resolve_task_layout(2) == TaskLayout::Multiple);
    assert(resolve_task_layout(4) == TaskLayout::Multiple);
    assert(std::strcmp(task_group_label(TraeTaskState::Pending), "PENDING TASKS") == 0);
    assert(std::strcmp(task_group_label(TraeTaskState::Running), "RUNNING TASKS") == 0);
    assert(task_group_color(TraeTaskState::Pending) == kPending);
    assert(task_group_color(TraeTaskState::Running) == kRunning);

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

}
