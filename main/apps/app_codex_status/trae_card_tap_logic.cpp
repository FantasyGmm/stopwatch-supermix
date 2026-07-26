#include "trae_card_tap_logic.h"

#include <cstdlib>

namespace {
constexpr int kMaximumDisplacementPx = 12;
constexpr std::uint32_t kMaximumDurationMs = 500;
}

void TraeCardTapLogic::press(int x, int y, std::uint32_t timestamp_ms, bool inside_deck)
{
    start_x_ = x;
    start_y_ = y;
    start_timestamp_ms_ = timestamp_ms;
    candidate_ = inside_deck;
    moved_too_far_ = false;
}

void TraeCardTapLogic::move(int x, int y)
{
    if (std::abs(x - start_x_) > kMaximumDisplacementPx ||
        std::abs(y - start_y_) > kMaximumDisplacementPx) {
        moved_too_far_ = true;
    }
}

bool TraeCardTapLogic::release(std::uint32_t timestamp_ms)
{
    const bool is_tap = candidate_ && !moved_too_far_ &&
                        timestamp_ms - start_timestamp_ms_ <= kMaximumDurationMs;
    candidate_ = false;
    return is_tap;
}

void TraeCardTapLogic::cancel()
{
    candidate_ = false;
}
