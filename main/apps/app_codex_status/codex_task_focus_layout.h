#pragma once

#include <array>
#include <cstddef>
#include <cstdint>


namespace codex_task_focus_layout {

constexpr int kViewportWidth = 466;
constexpr int kViewportHeight = 466;

constexpr uint32_t kBackground = 0x000000;
constexpr uint32_t kGraphite = 0x27303E;
constexpr uint32_t kBeaconViolet = 0x7C9DFF;
constexpr uint32_t kRunningCyan = 0x49C9EE;
constexpr uint32_t kSignalWhite = 0xF2F6F8;
constexpr uint32_t kEtchedGray = 0x788391;
constexpr uint32_t kPendingVermilion = 0xFF6248;

constexpr int kContentWidth = 336;
constexpr int kContentCenterX = kViewportWidth / 2 - 18;
constexpr int kContentX = kContentCenterX - kContentWidth / 2;
constexpr int kProjectY = 72;
constexpr int kProjectHeight = 24;
constexpr int kTaskTitleY = 154;
constexpr int kTaskTitleHeight = 104;
constexpr int kTaskTitleMaxLines = 3;
constexpr int kStatusCenterY = 305;
constexpr int kStatusHeight = 30;
constexpr int kStatusPendingWidth = 142;
constexpr int kStatusRunningWidth = 112;
constexpr int kStatusRadius = 15;
constexpr int kConnectionHintY = 344;
constexpr int kConnectionHintHeight = 22;
constexpr int kFocusTargetX = 36;
constexpr int kFocusTargetY = 120;
constexpr int kFocusTargetWidth = 358;
constexpr int kFocusTargetHeight = 210;
constexpr int kMinimumHitSize = 44;

constexpr int kDetailProjectY = 44;
constexpr int kDetailProjectHeight = 22;
constexpr int kDetailTitleY = 68;
constexpr int kDetailTitleHeight = 48;
constexpr int kDetailStatusY = 119;
constexpr int kDetailStatusHeight = 22;
constexpr int kDetailBodyX = 54;
constexpr int kDetailBodyY = 145;
constexpr int kDetailBodyWidth = 338;
constexpr int kDetailBodyHeight = 222;
constexpr int kDetailFooterX = 48;
constexpr int kDetailFooterY = 382;
constexpr int kDetailFooterWidth = 370;
constexpr int kDetailFooterHeight = 44;

constexpr int kTrackCenterX = 421;
constexpr int kTrackCenterY = 234;
constexpr int kTrackRadius = 132;
constexpr int kTrackSafeMinX = 378;
constexpr int kTrackSafeMaxX = 426;
constexpr int kTrackTopY = 132;
constexpr int kTrackBottomY = 338;
constexpr size_t kMaxTrackNodes = 8;
constexpr int kInactiveNodeSize = 8;
constexpr int kFocusedNodeWidth = 10;
constexpr int kFocusedNodeHeight = 24;
constexpr int kSelectedAccentWidth = 2;

constexpr bool kUsesCards = false;
constexpr bool kUsesGradients = false;
constexpr bool kUsesShadows = false;


enum class TaskTrackState : uint8_t {
    Pending,
    Running,
};


enum class TaskTrackShape : uint8_t {
    PendingDiamond,
    RunningCircle,
    FocusedCapsule,
};


struct TaskTrackNode {
    int center_x = 0;
    int center_y = 0;
    int width = 0;
    int height = 0;
    TaskTrackShape shape = TaskTrackShape::RunningCircle;
    uint32_t color = kGraphite;
    uint32_t accent_color = kGraphite;
    bool selected = false;
};


struct TaskTrackNodes {
    std::array<TaskTrackNode, kMaxTrackNodes> items{};
    size_t count = 0;
};


inline TaskTrackNodes make_track_nodes(
    const TaskTrackState* states,
    size_t count,
    size_t focused_index,
    size_t selected_index
)
{
    TaskTrackNodes result;
    if (states == nullptr || count == 0) {
        return result;
    }
    result.count = count < kMaxTrackNodes ? count : kMaxTrackNodes;

    constexpr std::array<int, kMaxTrackNodes> kArcX = {
        386, 399, 409, 416, 416, 409, 399, 386,
    };
    constexpr std::array<int, kMaxTrackNodes> kArcY = {
        134, 163, 192, 221, 250, 279, 308, 337,
    };

    for (size_t index = 0; index < result.count; ++index) {
        const size_t slot = result.count == 1
            ? 3
            : index * (kMaxTrackNodes - 1) / (result.count - 1);
        TaskTrackNode& node = result.items[index];
        node.center_x = kArcX[slot];
        node.center_y = kArcY[slot];
        node.selected = index == selected_index;
        node.color = states[index] == TaskTrackState::Pending
            ? kPendingVermilion
            : kRunningCyan;
        node.accent_color = node.selected ? kBeaconViolet : kGraphite;
        if (index == focused_index) {
            node.shape = TaskTrackShape::FocusedCapsule;
            node.width = kFocusedNodeWidth;
            node.height = kFocusedNodeHeight;
        } else {
            node.shape = states[index] == TaskTrackState::Pending
                ? TaskTrackShape::PendingDiamond
                : TaskTrackShape::RunningCircle;
            node.width = kInactiveNodeSize;
            node.height = kInactiveNodeSize;
        }
    }
    return result;
}

}  // namespace codex_task_focus_layout
