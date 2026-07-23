#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

inline uint16_t MeasurePcmMeanAbsoluteLevel(const int16_t* samples, std::size_t sample_count)
{
    if (samples == nullptr || sample_count == 0) {
        return 0;
    }

    uint64_t sum = 0;
    for (std::size_t index = 0; index < sample_count; ++index) {
        const int32_t sample = samples[index];
        sum += static_cast<uint32_t>(sample < 0 ? -sample : sample);
    }
    return static_cast<uint16_t>(std::min<uint64_t>(sum / sample_count, UINT16_MAX));
}

template <std::size_t LevelCount>
std::array<uint16_t, LevelCount> MeasurePcmEnvelopeLevels(
    const int16_t* samples, std::size_t sample_count)
{
    std::array<uint16_t, LevelCount> levels{};
    if (samples == nullptr || sample_count == 0) {
        return levels;
    }

    for (std::size_t level_index = 0; level_index < LevelCount; ++level_index) {
        const std::size_t begin = level_index * sample_count / LevelCount;
        const std::size_t end = (level_index + 1) * sample_count / LevelCount;
        if (end <= begin) {
            continue;
        }

        uint64_t sum = 0;
        uint32_t peak = 0;
        for (std::size_t sample_index = begin; sample_index < end; ++sample_index) {
            const int32_t sample = samples[sample_index];
            const uint32_t magnitude = static_cast<uint32_t>(sample < 0 ? -sample : sample);
            sum += magnitude;
            peak = std::max(peak, magnitude);
        }
        const uint32_t mean = static_cast<uint32_t>(sum / (end - begin));
        levels[level_index] = static_cast<uint16_t>(
            std::min<uint32_t>((mean * 48U + peak * 52U) / 100U, UINT16_MAX));
    }
    return levels;
}
