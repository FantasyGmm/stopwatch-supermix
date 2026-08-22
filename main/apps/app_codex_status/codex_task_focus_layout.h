#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>


namespace codex_task_focus_layout {

constexpr int kViewportWidth = 466;
constexpr int kViewportHeight = 466;
constexpr int kRoundSafeRadius = 221;

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
constexpr uint32_t kTaskRule = 0x27303A;
constexpr uint32_t kTaskSignalWhite = 0xF3F6F8;
constexpr uint32_t kTaskProject = 0x9DB2FF;
constexpr uint32_t kTaskMeta = 0x8995A2;
constexpr uint32_t kTaskCount = 0x8C98A5;
constexpr uint32_t kTaskHint = 0x687482;
constexpr uint32_t kTaskHandle = 0x778491;
constexpr uint32_t kTaskPending = 0xFF6248;
constexpr uint32_t kTaskRunning = 0x49C9ED;
constexpr uint32_t kTaskTrackInactive = 0x394452;
constexpr uint32_t kTaskTrackFocused = 0x829DFF;

constexpr int kContentWidth = 326;
constexpr int kContentCenterX = kViewportWidth / 2;
constexpr int kContentX = kContentCenterX - kContentWidth / 2;
constexpr size_t kDetailTitleMaxScalars = 30;
constexpr int kDetailBodyFontPx = 16;
constexpr int kConnectionHintY = 352;
constexpr int kConnectionHintHeight = 22;
constexpr int kMinimumHitSize = 44;

constexpr int kDeckDragHandleX = 209;
constexpr int kDeckDragHandleY = 12;
constexpr int kDeckDragHandleWidth = 48;
constexpr int kDeckDragHandleHeight = 4;
constexpr uint8_t kDeckHandleOpacity = 184;
constexpr int kDeckHeaderX = 140;
constexpr int kDeckHeaderWidth = 186;
constexpr bool kDeckHeaderCentered = true;
constexpr int kDeckCountX = 314;
constexpr int kDeckCountWidth = 40;
constexpr int kStreamFocusedTitleFontPx = 21;
constexpr int kStreamFocusedCjkScale = 256;
constexpr int kStreamNeighborCjkScale = 256;
constexpr int kFocusedProjectY = 3;
constexpr int kStreamProjectHeight = 18;
constexpr int kStreamFocusedTitleY = 23;
constexpr int kStreamDragLimit = 96;
constexpr int kStreamSeparatorTopY = 164;
constexpr int kStreamSeparatorBottomY = 278;
constexpr int kStreamSeparatorX = 37;
constexpr int kStreamSeparatorWidth = 392;
constexpr int kStreamSeparatorFadeWidth = 71;
constexpr int kStreamSeparatorCenterWidth = 250;
constexpr int kPreviousRowX = 79;
constexpr int kPreviousRowY = 84;
constexpr int kPreviousRowWidth = 308;
constexpr int kPreviousRowHeight = 70;
constexpr int kFocusedRowX = 47;
constexpr int kFocusedRowY = 168;
constexpr int kFocusedRowWidth = 373;
constexpr int kFocusedRowHeight = 107;
constexpr int kNextRowX = 70;
constexpr int kNextRowY = 286;
constexpr int kNextRowWidth = 326;
constexpr int kNextRowHeight = 70;

constexpr int kDetailHeaderX = 80;
constexpr int kDetailHeaderWidth = 306;
constexpr int kDetailDragHandleX = 209;
constexpr int kDetailDragHandleY = 12;
constexpr int kDetailDragHandleWidth = 48;
constexpr int kDetailDragHandleHeight = 4;
constexpr uint8_t kDetailHandleOpacity = 184;
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
constexpr int kDetailLiveDotX = 184;
constexpr int kDetailLiveDotY = 118;
constexpr int kDetailLiveDotSize = 5;
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
constexpr int kTrackGroupCenterY = 215;
constexpr int kTrackNodeGap = 6;
constexpr int kTrackSafeMaxX = 424;
constexpr size_t kMaxTrackNodes = 8;
constexpr int kInactiveNodeSize = 4;
constexpr int kFocusedNodeWidth = 8;
constexpr int kFocusedNodeHeight = 18;

constexpr bool kUsesCards = false;
constexpr bool kUsesGradients = true;
constexpr bool kUsesShadows = false;


enum class TaskStreamRole : uint8_t {
    Previous,
    Focused,
    Next,
};


struct TaskStreamSlot {
    bool visible = false;
    size_t task_index = 0;
    TaskStreamRole role = TaskStreamRole::Focused;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    uint8_t opacity = 0;
};


struct TaskStreamSlots {
    std::array<TaskStreamSlot, 3> items{};
};


struct TaskStreamTextLayout {
    int title_y = 20;
    int title_height = 23;
    int metadata_y = 50;
};


inline TaskStreamTextLayout make_stream_text_layout(
    bool focused,
    int measured_title_height,
    int font_line_height
)
{
    if (!focused) {
        return {};
    }
    constexpr int kSingleLineHeight = 25;
    constexpr int kTwoLineHeight = 54;
    const bool multiline = measured_title_height > font_line_height;
    return {
        kStreamFocusedTitleY,
        multiline
            ? std::max(kTwoLineHeight, measured_title_height)
            : std::max(kSingleLineHeight, measured_title_height),
        multiline ? 82 : 55,
    };
}


inline TaskStreamSlots make_stream_slots(size_t count, size_t focused_index)
{
    TaskStreamSlots result;
    result.items[0] = {
        focused_index > 0 && focused_index < count,
        focused_index > 0 ? focused_index - 1 : 0,
        TaskStreamRole::Previous,
        kPreviousRowX,
        kPreviousRowY,
        kPreviousRowWidth,
        kPreviousRowHeight,
        153,
    };
    result.items[1] = {
        focused_index < count,
        focused_index,
        TaskStreamRole::Focused,
        kFocusedRowX,
        kFocusedRowY,
        kFocusedRowWidth,
        kFocusedRowHeight,
        255,
    };
    result.items[2] = {
        focused_index + 1 < count,
        focused_index + 1,
        TaskStreamRole::Next,
        kNextRowX,
        kNextRowY,
        kNextRowWidth,
        kNextRowHeight,
        153,
    };
    return result;
}


inline bool rect_inside_round_safe_area(
    int x,
    int y,
    int width,
    int height,
    int center_x,
    int center_y,
    int radius
)
{
    const std::array<std::array<int, 2>, 4> corners = {{
        {{x, y}},
        {{x + width, y}},
        {{x, y + height}},
        {{x + width, y + height}},
    }};
    const int64_t radius_squared =
        static_cast<int64_t>(radius) * radius;
    for (const auto& corner : corners) {
        const int64_t dx = corner[0] - center_x;
        const int64_t dy = corner[1] - center_y;
        if (dx * dx + dy * dy > radius_squared) {
            return false;
        }
    }
    return true;
}


enum class TaskTrackState : uint8_t {
    Pending,
    Running,
};


enum class TaskTrackShape : uint8_t {
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
    const int node_height = has_focus
        ? kFocusedNodeHeight +
            kInactiveNodeSize * static_cast<int>(result.count - 1)
        : kInactiveNodeSize * static_cast<int>(result.count);
    const int occupied_height = node_height +
        kTrackNodeGap * static_cast<int>(result.count - 1);
    double top = kTrackGroupCenterY -
        static_cast<double>(occupied_height) / 2.0;

    for (size_t index = 0; index < result.count; ++index) {
        TaskTrackNode& node = result.items[index];
        node.center_x = kTrackCenterX;
        node.color = kTaskTrackInactive;
        if (index == focused_index) {
            node.shape = TaskTrackShape::FocusedCapsule;
            node.color = kTaskTrackFocused;
            node.width = kFocusedNodeWidth;
            node.height = kFocusedNodeHeight;
        } else {
            node.shape = TaskTrackShape::RunningCircle;
            node.width = kInactiveNodeSize;
            node.height = kInactiveNodeSize;
        }
        node.center_y = static_cast<int>(std::lround(
            top + static_cast<double>(node.height) / 2.0));
        top += node.height + kTrackNodeGap;
    }
    return result;
}

}  // namespace codex_task_focus_layout
