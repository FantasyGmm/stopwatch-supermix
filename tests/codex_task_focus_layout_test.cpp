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
    static_assert(kTaskRule == 0x27303A);
    static_assert(kTaskSignalWhite == 0xF3F6F8);
    static_assert(kTaskProject == 0x9DB2FF);
    static_assert(kTaskMeta == 0x8995A2);
    static_assert(kTaskCount == 0x8C98A5);
    static_assert(kTaskHint == 0x687482);
    static_assert(kTaskHandle == 0x778491);
    static_assert(kTaskPending == 0xFF6248);
    static_assert(kTaskRunning == 0x49C9ED);
    static_assert(kTaskTrackInactive == 0x394452);
    static_assert(kTaskTrackFocused == 0x829DFF);

    static_assert(kContentWidth == 326);
    static_assert(kContentCenterX == 233);
    static_assert(kContentX == 70);
    static_assert(kDetailBodyFontPx == 16);
    static_assert(kFocusedRowX == 47);
    static_assert(kFocusedRowY == 168);
    static_assert(kFocusedRowWidth == 373);
    static_assert(kFocusedRowHeight == 107);
    static_assert(kFocusedProjectY == 3);
    static_assert(kStreamProjectHeight == 18);
    static_assert(kStreamFocusedTitleY == 23);
    static_assert(
        kStreamFocusedTitleY -
            (kFocusedProjectY + kStreamProjectHeight) == 2);
    static_assert(kMinimumHitSize == 44);
    static_assert(kFocusedRowWidth >= kMinimumHitSize);
    static_assert(kFocusedRowHeight >= kMinimumHitSize);

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
    static_assert(kTrackGroupCenterY == 215);
    static_assert(kTrackNodeGap == 6);
    static_assert(kTrackSafeMaxX == 424);
    static_assert(kMaxTrackNodes == 8);
    static_assert(kInactiveNodeSize == 4);
    static_assert(kStreamSeparatorFadeWidth == 71);
    static_assert(kStreamSeparatorCenterWidth == 250);
    static_assert(
        kStreamSeparatorFadeWidth * 2 +
            kStreamSeparatorCenterWidth == kStreamSeparatorWidth);
    static_assert(!kUsesCards);
    static_assert(kUsesGradients);
    static_assert(!kUsesShadows);
}


void assert_center_stream_neighbors_and_round_safe_geometry()
{
    const auto first = make_stream_slots(5, 0);
    assert(!first.items[0].visible);
    assert(first.items[1].visible);
    assert(first.items[1].task_index == 0);
    assert(first.items[1].role == TaskStreamRole::Focused);
    assert(first.items[2].visible);
    assert(first.items[2].task_index == 1);

    const auto middle = make_stream_slots(5, 2);
    assert(middle.items[0].visible);
    assert(middle.items[0].task_index == 1);
    assert(middle.items[0].role == TaskStreamRole::Previous);
    assert(middle.items[1].task_index == 2);
    assert(middle.items[1].x == 47);
    assert(middle.items[1].y == 168);
    assert(middle.items[1].width == 373);
    assert(middle.items[1].height == 107);
    assert(middle.items[2].task_index == 3);
    assert(middle.items[0].opacity == 153);
    assert(middle.items[1].opacity == 255);
    assert(middle.items[2].opacity == 153);

    const auto last = make_stream_slots(5, 4);
    assert(last.items[0].visible);
    assert(last.items[0].task_index == 3);
    assert(last.items[1].task_index == 4);
    assert(!last.items[2].visible);

    for (const auto& slot : middle.items) {
        assert(slot.visible);
        assert(rect_inside_round_safe_area(
            slot.x, slot.y, slot.width, slot.height,
            kContentCenterX, kContentCenterX, kRoundSafeRadius));
    }
}


void assert_stream_text_tracks_actual_title_lines()
{
    const auto one_line = make_stream_text_layout(true, 25, 25);
    assert(one_line.title_y == 23);
    assert(one_line.title_height == 25);
    assert(one_line.metadata_y == 55);

    const auto two_lines = make_stream_text_layout(true, 54, 25);
    assert(two_lines.title_y == 23);
    assert(two_lines.title_height == 54);
    assert(two_lines.metadata_y == 82);

    const auto model_weight_ascii = make_stream_text_layout(true, 27, 27);
    assert(model_weight_ascii.title_y == 23);
    assert(model_weight_ascii.title_height == 27);
    assert(model_weight_ascii.metadata_y == 55);

    const auto model_weight_ascii_wrapped =
        make_stream_text_layout(true, 58, 27);
    assert(model_weight_ascii_wrapped.title_y == 23);
    assert(model_weight_ascii_wrapped.title_height == 58);
    assert(model_weight_ascii_wrapped.metadata_y == 82);

    const auto neighbor = make_stream_text_layout(false, 17, 17);
    assert(neighbor.title_y == 20);
    assert(neighbor.title_height == 23);
    assert(neighbor.metadata_y == 50);
}


void assert_center_stream_drag_follows_and_resists_boundaries()
{
    using namespace codex_task_focus_gesture;

    assert(task_stream_drag_offset(-40, true, true, 96) == -40);
    assert(task_stream_drag_offset(40, true, true, 96) == 40);
    assert(task_stream_drag_offset(-40, true, false, 96) == -10);
    assert(task_stream_drag_offset(40, false, true, 96) == 10);
    assert(task_stream_drag_offset(-150, true, true, 96) == -96);
    assert(task_stream_drag_offset(150, true, true, 96) == 96);
}


void assert_pull_handle_and_native_cjk_type_contract()
{
    using namespace codex_task_focus_gesture;

    static_assert(kDeckHeaderX + kDeckHeaderWidth / 2 == kContentCenterX);
    static_assert(kDeckHeaderCentered);
    static_assert(kStreamFocusedTitleFontPx == 21);
    static_assert(kDeckDragHandleX == 209);
    static_assert(kDeckDragHandleY == 12);
    static_assert(kDeckDragHandleWidth == 48);
    static_assert(kDeckDragHandleHeight == 4);
    static_assert(kDeckHandleOpacity == 184);
    static_assert(kStreamFocusedCjkScale == 256);
    static_assert(kStreamNeighborCjkScale == 256);

    assert(is_deck_handle_start(233, 24));
    assert(is_deck_handle_start(183, 0));
    assert(is_deck_handle_start(283, 48));
    assert(!is_deck_handle_start(182, 24));
    assert(!is_deck_handle_start(233, 49));

    const auto vertical = update_axis(
        TaskFocusGestureAxis::Undecided, 4, 48, 12);
    assert(should_dismiss_task_deck(
        4, 441, vertical, true, true, 465, 465));
    assert(!should_dismiss_task_deck(
        4, 440, vertical, true, true, 464, 465));
    assert(!should_dismiss_task_deck(
        4, 441, vertical, false, true, 465, 465));
    assert(!should_dismiss_task_deck(
        4, 441, vertical, true, false, 465, 465));
    assert(!should_dismiss_task_deck(
        4, -24, vertical, true, true, 465, 465));
    assert(task_deck_handle_drag_offset(48, 466) == 48);
    assert(task_deck_handle_drag_offset(-48, 466) == 0);
    assert(task_deck_handle_drag_offset(500, 466) == 466);
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
    const int occupied_height =
        kFocusedNodeHeight +
        kInactiveNodeSize * static_cast<int>(nodes.count - 1) +
        kTrackNodeGap * static_cast<int>(nodes.count - 1);
    double top = kTrackGroupCenterY - occupied_height / 2.0;
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
        top += node.height + kTrackNodeGap;
        if (index == 3) {
            assert(node.shape == TaskTrackShape::FocusedCapsule);
            assert(node.color == kTaskTrackFocused);
            assert(node.width == 8);
            assert(node.height == 18);
        } else {
            assert(node.shape == TaskTrackShape::RunningCircle);
            assert(node.color == kTaskTrackInactive);
        }
    }
    assert(nodes.items.front().center_y == 173);
    assert(nodes.items.back().center_y == 257);

    const std::array<TaskTrackState, 3> compact_states = {
        TaskTrackState::Running,
        TaskTrackState::Running,
        TaskTrackState::Running,
    };
    const auto compact = make_track_nodes(
        compact_states.data(), compact_states.size(), 1);
    assert(compact.items[0].center_y == 198);
    assert(compact.items[1].center_y == 215);
    assert(compact.items[2].center_y == 232);
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
    assert_center_stream_neighbors_and_round_safe_geometry();
    assert_stream_text_tracks_actual_title_lines();
    assert_center_stream_drag_follows_and_resists_boundaries();
    assert_pull_handle_and_native_cjk_type_contract();
    assert_track_node_semantics_and_geometry();
    assert_track_handles_short_and_empty_lists();
    assert_detail_right_swipe_is_axis_locked();
    assert_utf8_elision_is_bounded_and_valid();
    assert_detail_links_show_labels_without_destinations();
    assert_detail_scroll_resets_only_for_new_content();
    assert_deterministic_preview_scenarios();
}
