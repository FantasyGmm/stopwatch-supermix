#include "apps/app_codex_status/codex_pending_glow_logic.h"

#include <cassert>

using codex_pending_glow::Transition;

int main()
{
    using codex_pending_glow::desired_active;
    using codex_pending_glow::transition_for;

    assert(desired_active(CodexTaskState::Pending, false));
    assert(!desired_active(CodexTaskState::Pending, true));
    assert(!desired_active(CodexTaskState::Running, false));
    assert(!desired_active(CodexTaskState::Completed, false));

    assert(transition_for(false, false) == Transition::None);
    assert(transition_for(false, true) == Transition::Start);
    assert(transition_for(true, true) == Transition::None);
    assert(transition_for(true, false) == Transition::Stop);

    using namespace codex_pending_glow;

    static_assert(kHalfCycleMs == 600);
    static_assert(kFullCycleMs == 1200);
    static_assert(kPanelSize == 466);
    static_assert(kEdgeBandSize == 20);
    static_assert(kRings.size() == 3);
    static_assert(kInvalidationAreas.size() == 4);

    assert(opacity_for_phase(-1) == kMinimumOpacity);
    assert(opacity_for_phase(0) == kMinimumOpacity);
    assert(opacity_for_phase(128) > kMinimumOpacity);
    assert(opacity_for_phase(255) == kMaximumOpacity);
    assert(opacity_for_phase(300) == kMaximumOpacity);

    for (const RingGeometry& ring : kRings) {
        assert(ring.inset >= 0);
        assert(ring.size > 0);
        assert(ring.border_width > 0);
        assert(ring.inset + ring.size <= kPanelSize);
        assert(ring.inset + ring.border_width <= kEdgeBandSize);
    }

    assert((kInvalidationAreas[0] == EdgeArea{0, 0, 465, 19}));
    assert((kInvalidationAreas[1] == EdgeArea{0, 446, 465, 465}));
    assert((kInvalidationAreas[2] == EdgeArea{0, 20, 19, 445}));
    assert((kInvalidationAreas[3] == EdgeArea{446, 20, 465, 445}));
    return 0;
}
