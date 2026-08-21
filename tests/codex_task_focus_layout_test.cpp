#include "apps/app_codex_status/codex_task_focus_layout.h"
#include "apps/app_codex_status/codex_task_focus_gesture.h"
#include "apps/app_codex_status/codex_task_focus_preview.h"
#include "apps/app_codex_status/codex_task_focus_text.h"

#include <array>
#include <cassert>
#include <cmath>


namespace {

using namespace codex_task_focus_layout;


void assert_fixed_original_e_contract()
{
    static_assert(kViewportWidth == 466);
    static_assert(kViewportHeight == 466);
    static_assert(kBackground == 0x000000);
    static_assert(kGraphite == 0x27303E);
    static_assert(kBeaconViolet == 0x7C9DFF);
    static_assert(kRunningCyan == 0x49C9EE);
    static_assert(kSignalWhite == 0xF2F6F8);
    static_assert(kEtchedGray == 0x788391);
    static_assert(kPendingVermilion == 0xFF6248);
    static_assert(kProjectViolet == 0x9FB5FF);
    static_assert(kProjectPill == 0x161D30);
    static_assert(kPendingPill == 0xB54635);
    static_assert(kRunningText == 0x6ED8EF);
    static_assert(kRunningBorder == 0x318095);
    static_assert(kSecondaryText == 0xC8D2DC);
    static_assert(kDeckHint == 0x9BA6B1);
    static_assert(kDetailLabel == 0xB0BCC8);
    static_assert(kDetailState == 0xFF7F69);
    static_assert(kScrollbar == 0x4B5662);

    static_assert(kContentWidth == 326);
    static_assert(kContentCenterX == 233);
    static_assert(kContentX == 70);
    static_assert(kProjectY == 134);
    static_assert(kProjectHeight == 22);
    static_assert(kProjectPillHorizontalPadding == 16);
    static_assert(kTaskTitleX == 98);
    static_assert(kTaskTitleY == 177);
    static_assert(kTaskTitleWidth == 270);
    static_assert(kTaskTitleLineHeight == 33);
    static_assert(kTaskTitleHeight == 99);
    static_assert(kTaskTitleMaxLines == 3);
    static_assert(kLatinTitleScale == 266);
    static_assert(kCjkTitleScale == 432);
    static_assert(kDetailBodyFontPx == 16);
    static_assert(kStatusCenterY == 241);
    static_assert(kStatusHeight == 27);
    static_assert(kStatusPendingWidth == 90);
    static_assert(kStatusRunningWidth == 70);
    static_assert(kStatusRunningHeight == 29);
    static_assert(kDeckTargetY == 268);
    static_assert(kDeckOpenY == 294);
    static_assert(kDeckNextY == 411);
    static_assert(kFocusTargetX == 70);
    static_assert(kFocusTargetY == 120);
    static_assert(kFocusTargetWidth == 326);
    static_assert(kFocusTargetHeight == 230);
    static_assert(kMinimumHitSize == 44);
    static_assert(kFocusTargetWidth >= kMinimumHitSize);
    static_assert(kFocusTargetHeight >= kMinimumHitSize);

    static_assert(kDetailProjectY == 28);
    static_assert(kDetailDragHandleX == 209);
    static_assert(kDetailDragHandleY == 12);
    static_assert(kDetailDragHandleWidth == 48);
    static_assert(kDetailDragHandleHeight == 4);
    static_assert(kDetailHandleOpacity == 184);
    static_assert(kDetailTitleY == 47);
    static_assert(kDetailStatusY == 73);
    static_assert(kDetailLabelY == 105);
    static_assert(kDetailBodyX == 60);
    static_assert(kDetailBodyY == 132);
    static_assert(kDetailBodyWidth == 346);
    static_assert(kDetailBodyHeight == 253);
    static_assert(kDetailReplyLineSpace == 5);
    static_assert(kDetailReplyLineHeight == 25);
    static_assert(kDetailParagraphGap == 13);
    static_assert(kDetailParagraphLimit == 16);
    static_assert(kDetailFooterY == 401);
    static_assert(kDetailFooterY >= kDetailBodyY + kDetailBodyHeight);
    static_assert(kDetailFooterY + kDetailFooterHeight <= kViewportHeight);

    static_assert(kTrackCenterX == 420);
    static_assert(kTrackTopY == 147);
    static_assert(kTrackHeight == 195);
    static_assert(kTrackSafeMaxX == 424);
    static_assert(kMaxTrackNodes == 8);
    static_assert(!kUsesCards);
    static_assert(!kUsesGradients);
    static_assert(!kUsesShadows);
}


void assert_detail_right_swipe_is_axis_locked()
{
    using namespace codex_task_focus_gesture;

    const auto horizontal = update_axis(
        TaskFocusGestureAxis::Undecided, 40, 8, 12);
    assert(horizontal == TaskFocusGestureAxis::Horizontal);
    assert(is_detail_return_gesture(
        40, 8, horizontal, false, 36));
    assert(!is_detail_return_gesture(
        -40, 8, horizontal, false, 36));
    assert(!is_detail_return_gesture(
        35, 3, horizontal, false, 36));

    const auto vertical = update_axis(
        TaskFocusGestureAxis::Undecided, 7, 40, 12);
    assert(vertical == TaskFocusGestureAxis::Vertical);
    assert(is_detail_return_gesture(
        7, 40, vertical, true, 36));
    assert(!is_detail_return_gesture(
        7, 40, vertical, false, 36));
}


void assert_track_node_semantics_and_geometry()
{
    const std::array<TaskTrackState, 8> states = {
        TaskTrackState::Pending,
        TaskTrackState::Pending,
        TaskTrackState::Pending,
        TaskTrackState::Running,
        TaskTrackState::Running,
        TaskTrackState::Running,
        TaskTrackState::Running,
        TaskTrackState::Running,
    };
    const auto nodes = make_track_nodes(states.data(), states.size(), 3);

    assert(nodes.count == 8);
    int previous_y = -1;
    const double gap = static_cast<double>(
        kTrackHeight - kFocusedNodeHeight -
        kInactiveNodeSize * static_cast<int>(nodes.count - 1)) /
        static_cast<double>(nodes.count);
    double top = kTrackTopY + gap / 2.0;
    for (size_t index = 0; index < nodes.count; ++index) {
        const auto& node = nodes.items[index];
        assert(node.center_x <= kTrackSafeMaxX);
        assert(node.center_x == kTrackCenterX);
        assert(node.center_y > kTrackTopY);
        assert(node.center_y < kTrackTopY + kTrackHeight);
        assert(node.center_y > previous_y);
        previous_y = node.center_y;
        const int expected_y = static_cast<int>(std::lround(
            top + static_cast<double>(node.height) / 2.0));
        assert(std::abs(node.center_y - expected_y) <= 1);
        top += node.height + gap;
        if (index == 3) {
            assert(node.shape == TaskTrackShape::FocusedCapsule);
            assert(node.color == kBeaconViolet);
            assert(node.width == 8);
            assert(node.height == 18);
        } else if (index < 3) {
            assert(node.shape == TaskTrackShape::PendingDiamond);
            assert(node.color == kPendingVermilion);
        } else {
            assert(node.shape == TaskTrackShape::RunningCircle);
            assert(node.color == kRunningCyan);
        }
    }
}


void assert_track_handles_short_and_empty_lists()
{
    const std::array<TaskTrackState, 2> states = {
        TaskTrackState::Pending,
        TaskTrackState::Running,
    };
    const auto nodes = make_track_nodes(states.data(), states.size(), 0);
    assert(nodes.count == 2);
    assert(nodes.items[0].shape == TaskTrackShape::FocusedCapsule);
    assert(nodes.items[0].center_y < nodes.items[1].center_y);

    const auto empty = make_track_nodes(nullptr, 0, 0);
    assert(empty.count == 0);
}


void assert_utf8_elision_is_bounded_and_valid()
{
    assert(elide_utf8("short", 5) == "short");
    assert(elide_utf8("abcdef", 5) == "abcd…");
    assert(elide_utf8("中文标题安全", 5) == "中文标题…");
    assert(elide_utf8("😀😀😀😀", 3) == "😀😀…");
    assert(elide_utf8("anything", 1) == "…");
    assert(elide_utf8(nullptr, 5).empty());
    assert(utf8_scalar_count(elide_utf8("中文标题安全", 5)) == 5);
}


void assert_detail_links_show_labels_without_destinations()
{
    assert(detail_display_text(
        "查看 [AGENTS.md](/Users/luke/Projects/StopWatch/AGENTS.md) 后继续") ==
        "查看 AGENTS.md 后继续");
    assert(detail_display_text(
        "参考 [Codex 文档](https://example.com/docs?q=watch) 和正文") ==
        "参考 Codex 文档 和正文");
    assert(detail_display_text("保留不完整的 [链接](地址") ==
           "保留不完整的 [链接](地址");
    assert(detail_display_text(nullptr).empty());
}


void assert_detail_scroll_resets_only_for_new_content()
{
    TaskDetailScrollIdentity identity;
    assert(identity.update(7, -1));
    assert(!identity.update(7, -1));
    assert(identity.update(7, 0));
    assert(!identity.update(7, 0));
    assert(identity.update(8, 0));
    identity.reset();
    assert(identity.update(8, 0));
}


void assert_deterministic_preview_scenarios()
{
    const auto pending = task_focus_preview(TaskFocusPreviewScenario::PendingDeck);
    assert(!pending.detail);
    assert(pending.count == 5);
    assert(pending.focused_index == 0);
    assert(pending.states[0] == TaskTrackState::Pending);
    assert(pending.project[0] != '\0');
    assert(pending.title[0] != '\0');

    const auto running = task_focus_preview(TaskFocusPreviewScenario::RunningDeck);
    assert(running.states[running.focused_index] == TaskTrackState::Running);

    const auto boundary = task_focus_preview(TaskFocusPreviewScenario::MixedBoundary);
    assert(boundary.states[boundary.focused_index - 1] == TaskTrackState::Pending);
    assert(boundary.states[boundary.focused_index] == TaskTrackState::Running);

    const auto detail = task_focus_preview(TaskFocusPreviewScenario::LongCjkDetail);
    assert(detail.detail);
    assert(detail.reply[0] != '\0');

    const auto original_e = task_focus_preview(
        TaskFocusPreviewScenario::OriginalEDetail);
    assert(original_e.detail);
    assert(std::string(original_e.project) == "StopWatch");
    assert(std::string(original_e.title) == "修复语音输入提前结束");

    const auto empty = task_focus_preview(TaskFocusPreviewScenario::EmptyDeck);
    assert(empty.count == 0);

    const auto recording = task_focus_preview(TaskFocusPreviewScenario::RecordingDetail);
    assert(recording.detail);
    assert(recording.recording);
}

}  // namespace


int main()
{
    assert_fixed_original_e_contract();
    assert_track_node_semantics_and_geometry();
    assert_track_handles_short_and_empty_lists();
    assert_detail_right_swipe_is_axis_locked();
    assert_utf8_elision_is_bounded_and_valid();
    assert_detail_links_show_labels_without_destinations();
    assert_detail_scroll_resets_only_for_new_content();
    assert_deterministic_preview_scenarios();
}
