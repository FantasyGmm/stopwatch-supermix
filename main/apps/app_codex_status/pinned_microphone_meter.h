#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

constexpr std::size_t kPinnedMicrophoneInputLevelCount = 15;
using PinnedMicrophoneInputLevels =
    std::array<uint16_t, kPinnedMicrophoneInputLevelCount>;

struct PinnedMicrophoneMeterFrame {
    bool visible = false;
    std::array<uint8_t, 4> heights{};
};

PinnedMicrophoneMeterFrame pinned_microphone_meter_frame(
    bool active,
    const PinnedMicrophoneInputLevels& levels);
