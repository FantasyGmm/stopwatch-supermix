#pragma once

#include "codex_status_model.h"

#include <array>
#include <cstdint>

namespace codex_pending_glow {

constexpr int kPanelSize = 466;
constexpr int kHalfCycleMs = 800;
constexpr int kFullCycleMs = kHalfCycleMs * 2;

struct RingGeometry {
    int inset;
    int size;
    int border_width;
    uint8_t minimum_opacity;
    uint8_t maximum_opacity;
};

inline constexpr std::array<RingGeometry, 3> kRings{{
    {1, 464, 2, 102, 255},
    {5, 456, 4, 51, 153},
    {11, 444, 6, 25, 76},
}};

enum class Transition {
    None,
    Start,
    Stop,
};

constexpr int32_t bounded_phase(int32_t phase)
{
    return phase < 0 ? 0 : (phase > 255 ? 255 : phase);
}

constexpr uint8_t opacity_for_phase(
    int32_t phase,
    uint8_t minimum,
    uint8_t maximum
)
{
    const int32_t bounded = bounded_phase(phase);
    return static_cast<uint8_t>(
        minimum + bounded * (maximum - minimum) / 255
    );
}

constexpr uint8_t static_ring_opacity(uint8_t minimum, uint8_t maximum)
{
    return static_cast<uint8_t>(
        minimum + (static_cast<uint16_t>(maximum) - minimum) / 2
    );
}

constexpr bool pulse_desired(
    CodexTaskState state,
    bool screen_off,
    bool surface_visible = true)
{
    return state != CodexTaskState::Completed && !screen_off && surface_visible;
}

constexpr bool ring_desired(
    CodexTaskState state,
    bool screen_off,
    bool surface_visible = true)
{
    return state == CodexTaskState::Pending && !screen_off && surface_visible;
}

constexpr Transition transition_for(bool active, bool requested_active)
{
    if (active == requested_active) {
        return Transition::None;
    }
    return requested_active ? Transition::Start : Transition::Stop;
}

}  // namespace codex_pending_glow
