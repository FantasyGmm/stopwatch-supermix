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

template <std::size_t SampleLimit>
uint16_t MeasurePcmSparseLevel(const int16_t* samples, std::size_t sample_count)
{
    static_assert(SampleLimit > 0);
    static_assert(SampleLimit <= UINT32_MAX / 32768U);
    if (samples == nullptr || sample_count == 0) {
        return 0;
    }

    const std::size_t sampled_count = std::min<std::size_t>(sample_count, SampleLimit);
    uint32_t sum = 0;
    uint32_t peak = 0;
    for (std::size_t index = 0; index < sampled_count; ++index) {
        const std::size_t sample_index =
            ((index * 2U + 1U) * sample_count) / (sampled_count * 2U);
        const int32_t sample = samples[std::min(sample_index, sample_count - 1)];
        const uint32_t magnitude = static_cast<uint32_t>(sample < 0 ? -sample : sample);
        sum += magnitude;
        peak = std::max(peak, magnitude);
    }
    const uint32_t mean = sum / sampled_count;
    return static_cast<uint16_t>(
        std::min<uint32_t>((mean * 48U + peak * 52U) / 100U, UINT16_MAX));
}

template <std::size_t LevelCount>
std::array<uint16_t, LevelCount> OrderPcmLevelHistory(
    const std::array<uint16_t, LevelCount>& slots, uint32_t write_count)
{
    static_assert(LevelCount > 0);
    std::array<uint16_t, LevelCount> ordered{};
    const std::size_t valid_count =
        std::min<std::size_t>(write_count, LevelCount);
    const std::size_t leading_zeros = LevelCount - valid_count;
    const std::size_t oldest_slot =
        write_count >= LevelCount ? write_count % LevelCount : 0;
    for (std::size_t index = 0; index < valid_count; ++index) {
        ordered[leading_zeros + index] =
            slots[(oldest_slot + index) % LevelCount];
    }
    return ordered;
}
