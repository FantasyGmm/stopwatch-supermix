#include "pinned_microphone_meter.h"

#include <algorithm>

namespace {

constexpr uint32_t kMinimumHeight = 12;
constexpr uint32_t kMaximumHeight = 64;
constexpr uint32_t kSaturationLevel = 8192;

uint8_t level_height(uint16_t level)
{
    const uint32_t bounded = std::min<uint32_t>(level, kSaturationLevel);
    return static_cast<uint8_t>(
        kMinimumHeight +
        (bounded * (kMaximumHeight - kMinimumHeight)) / kSaturationLevel);
}

}  // namespace

PinnedMicrophoneMeterFrame pinned_microphone_meter_frame(
    bool active,
    const PinnedMicrophoneInputLevels& levels)
{
    PinnedMicrophoneMeterFrame frame{};
    if (!active) {
        return frame;
    }

    frame.visible = true;
    constexpr std::size_t kVisibleLevelCount = 4;
    const std::size_t first = levels.size() - kVisibleLevelCount;
    for (std::size_t index = 0; index < frame.heights.size(); ++index) {
        frame.heights[index] = level_height(levels[first + index]);
    }
    return frame;
}
