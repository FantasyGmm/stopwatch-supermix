#include "apps/app_codex_status/codex_pending_glow_logic.h"

#include <cassert>
#include <cmath>

using codex_pending_glow::Transition;

namespace {

bool invalidation_covers(double x, double y)
{
    for (const auto& area : codex_pending_glow::kInvalidationAreas) {
        if (x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main()
{
    using codex_pending_glow::pulse_desired;
    using codex_pending_glow::ring_desired;
    using codex_pending_glow::transition_for;

    assert(pulse_desired(CodexTaskState::Pending, false));
    assert(pulse_desired(CodexTaskState::Running, false));
    assert(!pulse_desired(CodexTaskState::Pending, true));
    assert(!pulse_desired(CodexTaskState::Completed, false));
    assert(ring_desired(CodexTaskState::Pending, false));
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
    static_assert(kInvalidationAreas.size() == 12);

    assert(opacity_for_phase(-1, 102, 255) == 102);
    assert(opacity_for_phase(0, 102, 255) == 102);
    assert(opacity_for_phase(128, 102, 255) > 102);
    assert(opacity_for_phase(255, 102, 255) == 255);
    assert(opacity_for_phase(300, 102, 255) == 255);

    assert(kRings[0].minimum_opacity > kRings[1].minimum_opacity);
    assert(kRings[1].minimum_opacity > kRings[2].minimum_opacity);
    assert(kRings[0].maximum_opacity > kRings[1].maximum_opacity);
    assert(kRings[1].maximum_opacity > kRings[2].maximum_opacity);

    for (const RingGeometry& ring : kRings) {
        assert(ring.inset >= 0);
        assert(ring.size > 0);
        assert(ring.border_width > 0);
        assert(ring.inset + ring.size <= kPanelSize);

        const double center = ring.inset + (ring.size - 1) / 2.0;
        const double outer_radius = (ring.size - 1) / 2.0;
        for (int angle_tenths = 0; angle_tenths < 3600; ++angle_tenths) {
            const double radians = angle_tenths * 3.14159265358979323846 / 1800.0;
            for (int stroke_offset = 0; stroke_offset <= ring.border_width; ++stroke_offset) {
                const double radius = outer_radius - stroke_offset;
                const double x = center + radius * std::cos(radians);
                const double y = center + radius * std::sin(radians);
                assert(invalidation_covers(x, y));
            }
        }
    }

    int invalidated_pixels = 0;
    for (const EdgeArea& area : kInvalidationAreas) {
        assert(area.x1 >= 0);
        assert(area.y1 >= 0);
        assert(area.x2 < kPanelSize);
        assert(area.y2 < kPanelSize);
        assert(area.x1 <= area.x2);
        assert(area.y1 <= area.y2);
        invalidated_pixels += (area.x2 - area.x1 + 1) * (area.y2 - area.y1 + 1);
    }
    assert(invalidated_pixels < kPanelSize * kPanelSize / 2);
    return 0;
}
