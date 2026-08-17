#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>


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
constexpr uint32_t kProjectViolet = 0x9FB5FF;
constexpr uint32_t kProjectPill = 0x161D30;
constexpr uint32_t kPendingPill = 0xB54635;
constexpr uint32_t kRunningText = 0x6ED8EF;
constexpr uint32_t kRunningBorder = 0x318095;
constexpr uint32_t kSecondaryText = 0xC8D2DC;
constexpr uint32_t kDeckHint = 0x9BA6B1;
constexpr uint32_t kDetailLabel = 0xB0BCC8;
constexpr uint32_t kDetailState = 0xFF7F69;
constexpr uint32_t kScrollbar = 0x4B5662;

constexpr int kContentWidth = 326;
constexpr int kContentCenterX = kViewportWidth / 2;
constexpr int kContentX = kContentCenterX - kContentWidth / 2;
constexpr int kProjectY = 134;
constexpr int kProjectHeight = 22;
constexpr int kProjectPillHorizontalPadding = 16;
constexpr int kTaskTitleX = 98;
constexpr int kTaskTitleY = 177;
constexpr int kTaskTitleWidth = 270;
constexpr int kTaskTitleLineHeight = 33;
constexpr int kTaskTitleHeight = kTaskTitleLineHeight * 3;
constexpr int kTaskTitleMaxLines = 3;
constexpr size_t kDeckTitleCjkMaxScalars = 30;
constexpr size_t kDeckTitleLatinMaxScalars = 48;
constexpr size_t kDetailTitleMaxScalars = 30;
constexpr int kLatinTitleScale = 266;  // 26 px source rendered at 27 px.
constexpr int kCjkTitleScale = 432;   // 16 px source rendered at 27 px.
constexpr int kDetailBodyFontPx = 16;
constexpr int kStatusCenterY = 241;
constexpr int kStatusHeight = 27;
constexpr int kStatusPendingWidth = 90;
constexpr int kStatusRunningWidth = 70;
constexpr int kStatusRunningHeight = 29;
constexpr int kStatusRadius = 14;
constexpr int kDeckTargetY = 268;
constexpr int kDeckTargetHeight = 13;
constexpr int kDeckOpenY = 294;
constexpr int kDeckOpenHeight = 23;
constexpr int kDeckNextY = 411;
constexpr int kDeckNextHeight = 23;
constexpr int kConnectionHintY = 352;
constexpr int kConnectionHintHeight = 22;
constexpr int kFocusTargetX = 70;
constexpr int kFocusTargetY = 120;
constexpr int kFocusTargetWidth = 326;
constexpr int kFocusTargetHeight = 230;
constexpr int kMinimumHitSize = 44;

constexpr int kDetailHeaderX = 80;
constexpr int kDetailHeaderWidth = 306;
constexpr int kDetailProjectY = 28;
constexpr int kDetailProjectHeight = 12;
constexpr int kDetailTitleY = 47;
constexpr int kDetailTitleHeight = 20;
constexpr int kDetailStatusY = 73;
constexpr int kDetailStatusHeight = 13;
constexpr int kDetailLabelX = 108;
constexpr int kDetailLabelY = 105;
constexpr int kDetailLabelWidth = 250;
constexpr int kDetailLabelHeight = 24;
constexpr int kDetailLabelTextY = 114;
constexpr int kDetailBodyX = 60;
constexpr int kDetailBodyY = 132;
constexpr int kDetailBodyWidth = 346;
constexpr int kDetailBodyHeight = 253;
constexpr int kDetailReplyLineSpace = 5;
constexpr int kDetailReplyLineHeight = 25;
constexpr int kDetailParagraphGap = 13;
constexpr size_t kDetailParagraphLimit = 16;
constexpr int kDetailFooterX = 116;
constexpr int kDetailFooterY = 401;
constexpr int kDetailFooterWidth = 234;
constexpr int kDetailFooterHeight = 31;

constexpr int kTrackCenterX = 420;
constexpr int kTrackTopY = 147;
constexpr int kTrackHeight = 195;
constexpr int kTrackSafeMaxX = 424;
constexpr size_t kMaxTrackNodes = 8;
constexpr int kInactiveNodeSize = 6;
constexpr int kFocusedNodeWidth = 8;
constexpr int kFocusedNodeHeight = 18;

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
};


struct TaskTrackNodes {
    std::array<TaskTrackNode, kMaxTrackNodes> items{};
    size_t count = 0;
};


inline TaskTrackNodes make_track_nodes(
    const TaskTrackState* states,
    size_t count,
    size_t focused_index
)
{
    TaskTrackNodes result;
    if (states == nullptr || count == 0) {
        return result;
    }
    result.count = count < kMaxTrackNodes ? count : kMaxTrackNodes;
    const bool has_focus = focused_index < result.count;
    const int occupied_height = has_focus
        ? kFocusedNodeHeight +
            kInactiveNodeSize * static_cast<int>(result.count - 1)
        : kInactiveNodeSize * static_cast<int>(result.count);
    const double gap = static_cast<double>(kTrackHeight - occupied_height) /
        static_cast<double>(result.count);
    double top = kTrackTopY + gap / 2.0;

    for (size_t index = 0; index < result.count; ++index) {
        TaskTrackNode& node = result.items[index];
        node.center_x = kTrackCenterX;
        node.color = states[index] == TaskTrackState::Pending
            ? kPendingVermilion
            : kRunningCyan;
        if (index == focused_index) {
            node.shape = TaskTrackShape::FocusedCapsule;
            node.color = kBeaconViolet;
            node.width = kFocusedNodeWidth;
            node.height = kFocusedNodeHeight;
        } else {
            node.shape = states[index] == TaskTrackState::Pending
                ? TaskTrackShape::PendingDiamond
                : TaskTrackShape::RunningCircle;
            node.width = kInactiveNodeSize;
            node.height = kInactiveNodeSize;
        }
        node.center_y = static_cast<int>(std::lround(
            top + static_cast<double>(node.height) / 2.0));
        top += node.height + gap;
    }
    return result;
}

}  // namespace codex_task_focus_layout
