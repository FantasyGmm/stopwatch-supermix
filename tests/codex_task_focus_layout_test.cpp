#include "apps/app_codex_status/codex_task_focus_layout.h"
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

    static_assert(kContentWidth == 336);
    static_assert(kContentCenterX == 215);
    static_assert(kContentX == 47);
    static_assert(kProjectY == 72);
    static_assert(kTaskTitleY == 154);
    static_assert(kTaskTitleHeight == 104);
    static_assert(kTaskTitleMaxLines == 3);
    static_assert(kLatinTitleScale == 315);
    static_assert(kCjkTitleScale == 476);
    static_assert(kCjkBodyScale == 293);
    static_assert(kStatusCenterY == 305);
    static_assert(kStatusHeight == 30);
    static_assert(kFocusTargetX == 36);
    static_assert(kFocusTargetY == 120);
    static_assert(kFocusTargetWidth == 358);
    static_assert(kFocusTargetHeight == 210);
    static_assert(kMinimumHitSize == 44);
    static_assert(kFocusTargetWidth >= kMinimumHitSize);
    static_assert(kFocusTargetHeight >= kMinimumHitSize);

    static_assert(kDetailBodyX == 54);
    static_assert(kDetailBodyY == 145);
    static_assert(kDetailBodyWidth == 338);
    static_assert(kDetailBodyHeight == 222);
    static_assert(kDetailFooterY >= kDetailBodyY + kDetailBodyHeight);
    static_assert(kDetailFooterY + kDetailFooterHeight <= kViewportHeight);

    static_assert(kTrackCenterX == 421);
    static_assert(kTrackCenterY == 234);
    static_assert(kTrackRadius == 132);
    static_assert(kTrackSafeMaxX == 426);
    static_assert(kMaxTrackNodes == 8);
    static_assert(!kUsesCards);
    static_assert(!kUsesGradients);
    static_assert(!kUsesShadows);
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
    for (size_t index = 0; index < nodes.count; ++index) {
        const auto& node = nodes.items[index];
        assert(node.center_x <= kTrackSafeMaxX);
        assert(node.center_x >= kTrackCenterX - kTrackRadius);
        assert(node.center_y >= kTrackTopY);
        assert(node.center_y <= kTrackBottomY);
        assert(node.center_y > previous_y);
        previous_y = node.center_y;
        const int delta_x = node.center_x - kTrackCenterX;
        const int delta_y = node.center_y - kTrackCenterY;
        const double radius = std::sqrt(delta_x * delta_x + delta_y * delta_y);
        assert(std::abs(radius - kTrackRadius) <= kTrackRadiusTolerance);
        if (index == 3) {
            assert(node.shape == TaskTrackShape::FocusedCapsule);
            assert(node.color == kBeaconViolet);
            assert(node.width == kFocusedNodeWidth);
            assert(node.height == kFocusedNodeHeight);
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
    assert_utf8_elision_is_bounded_and_valid();
    assert_deterministic_preview_scenarios();
}
