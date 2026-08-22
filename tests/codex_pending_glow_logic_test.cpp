#include "apps/app_codex_status/codex_pending_glow_logic.h"

#include <cassert>
using codex_pending_glow::Transition;

int main()
{
    using codex_pending_glow::pulse_desired;
    using codex_pending_glow::ring_desired;
    using codex_pending_glow::transition_for;

    assert(pulse_desired(CodexTaskState::Pending, false));
    assert(pulse_desired(CodexTaskState::Running, false));
    assert(!pulse_desired(CodexTaskState::Pending, false, false));
    assert(!pulse_desired(CodexTaskState::Pending, true));
    assert(!pulse_desired(CodexTaskState::Completed, false));
    assert(ring_desired(CodexTaskState::Pending, false));
    assert(!ring_desired(CodexTaskState::Pending, false, false));
    assert(!ring_desired(CodexTaskState::Running, false));
    assert(!ring_desired(CodexTaskState::Pending, true));

    assert(transition_for(false, false) == Transition::None);
    assert(transition_for(false, true) == Transition::Start);
    assert(transition_for(true, true) == Transition::None);
    assert(transition_for(true, false) == Transition::Stop);

    using namespace codex_pending_glow;

    static_assert(kHalfCycleMs == 800);
    static_assert(kFullCycleMs == 1600);
    static_assert(kPanelSize == 466);
    static_assert(kRings.size() == 3);
    static_assert(kRingPhaseStep == 32);

    assert(opacity_for_phase(-1, 102, 255) == 102);
    assert(opacity_for_phase(0, 102, 255) == 102);
    assert(opacity_for_phase(128, 102, 255) > 102);
    assert(opacity_for_phase(255, 102, 255) == 255);
    assert(opacity_for_phase(300, 102, 255) == 255);
    assert(ring_phase_bucket(0) == 0);
    assert(ring_phase_bucket(31) == 0);
    assert(ring_phase_bucket(32) == 1);
    assert(ring_phase_bucket(255) == 7);
    assert(ring_redraw_desired(-1, 0, true));
    assert(!ring_redraw_desired(0, 31, true));
    assert(ring_redraw_desired(0, 32, true));
    assert(!ring_redraw_desired(0, 32, false));

    assert(kRings[0].minimum_opacity > kRings[1].minimum_opacity);
    assert(kRings[1].minimum_opacity > kRings[2].minimum_opacity);
    assert(kRings[0].maximum_opacity > kRings[1].maximum_opacity);
    assert(kRings[1].maximum_opacity > kRings[2].maximum_opacity);

    for (const RingGeometry& ring : kRings) {
        assert(ring.inset >= 0);
        assert(ring.size > 0);
        assert(ring.border_width > 0);
        assert(ring.inset + ring.size <= kPanelSize);
    }
    return 0;
}
