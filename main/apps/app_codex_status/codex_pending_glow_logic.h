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

struct EdgeArea {
    int x1;
    int y1;
    int x2;
    int y2;
};

inline constexpr std::array<RingGeometry, 3> kRings{{
    {1, 464, 2, 102, 255},
    {5, 456, 4, 51, 153},
    {11, 444, 6, 25, 76},
}};

inline constexpr std::array<EdgeArea, 12> kInvalidationAreas{{
    {414, 230, 465, 351},
    {336, 336, 437, 437},
    {230, 414, 351, 465},
    {114, 414, 235, 465},
    {28, 336, 129, 437},
    {0, 230, 51, 351},
    {0, 114, 51, 235},
    {28, 28, 129, 129},
    {113, 0, 235, 51},
    {230, 0, 351, 51},
    {336, 28, 437, 129},
    {414, 113, 465, 235},
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
