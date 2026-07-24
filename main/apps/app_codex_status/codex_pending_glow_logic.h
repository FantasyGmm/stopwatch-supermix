#pragma once

#include "codex_status_model.h"

#include <array>
#include <cstdint>

namespace codex_pending_glow {

constexpr int kPanelSize = 466;
constexpr int kHalfCycleMs = 600;
constexpr int kFullCycleMs = kHalfCycleMs * 2;
constexpr int kEdgeBandSize = 20;
constexpr uint8_t kMinimumOpacity = 102;
constexpr uint8_t kMaximumOpacity = 255;

struct RingGeometry {
    int inset;
    int size;
    int border_width;
};

struct EdgeArea {
    int x1;
    int y1;
    int x2;
    int y2;
};

constexpr bool operator==(EdgeArea lhs, EdgeArea rhs)
{
    return lhs.x1 == rhs.x1 && lhs.y1 == rhs.y1 &&
           lhs.x2 == rhs.x2 && lhs.y2 == rhs.y2;
}

inline constexpr std::array<RingGeometry, 3> kRings{{
    {1, 464, 2},
    {5, 456, 4},
    {11, 444, 6},
}};

inline constexpr std::array<EdgeArea, 4> kInvalidationAreas{{
    {0, 0, 465, 19},
    {0, 446, 465, 465},
    {0, 20, 19, 445},
    {446, 20, 465, 445},
}};

enum class Transition {
    None,
    Start,
    Stop,
};

constexpr uint8_t opacity_for_phase(int32_t phase)
{
    const int32_t bounded = phase < 0 ? 0 : (phase > 255 ? 255 : phase);
    return static_cast<uint8_t>(
        kMinimumOpacity +
        bounded * (kMaximumOpacity - kMinimumOpacity) / 255
    );
}

constexpr bool desired_active(CodexTaskState state, bool screen_off)
{
    return state == CodexTaskState::Pending && !screen_off;
}

constexpr Transition transition_for(bool active, bool requested_active)
{
    if (active == requested_active) {
        return Transition::None;
    }
    return requested_active ? Transition::Start : Transition::Stop;
}

}  // namespace codex_pending_glow
