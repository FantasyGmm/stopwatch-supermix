#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

class AssistantInputWaveform {
public:
    static constexpr std::size_t level_count = 15;
    using InputLevels = std::array<uint16_t, level_count>;
    using Frame = std::array<int16_t, level_count>;

    void reset()
    {
        _noise_floor = 0.0f;
        _display_levels.fill(0.0f);
        _normalization_level = 0.0f;
    }

    Frame update(const InputLevels& pcm_levels)
    {
        std::array<float, level_count> raw_levels{};
        float top1 = 0.0f;
        float top2 = 0.0f;
        float top3 = 0.0f;
        const auto quietest = std::min_element(pcm_levels.begin(), pcm_levels.end());
        const float quiet_reference = static_cast<float>(*quietest);
        const float floor_alpha = quiet_reference < _noise_floor ? 0.45f : 0.004f;
        _noise_floor += (quiet_reference - _noise_floor) * floor_alpha;

        for (std::size_t index = 0; index < level_count; ++index) {
            const float sample = static_cast<float>(pcm_levels[index]);
            const float raw = std::max(sample - (_noise_floor * 2.20f + 60.0f), 0.0f);
            raw_levels[index] = raw;

            if (raw >= top1) {
                top3 = top2;
                top2 = top1;
                top1 = raw;
            } else if (raw >= top2) {
                top3 = top2;
                top2 = raw;
            } else if (raw > top3) {
                top3 = raw;
            }
        }

        const float frame_reference = top1 * 0.80f + top2 * 0.14f + top3 * 0.06f;
        if (frame_reference > 0.0f) {
            if (_normalization_level <= 0.0f) {
                _normalization_level = frame_reference;
            } else {
                const float norm_alpha = frame_reference > _normalization_level ? 0.44f : 0.16f;
                _normalization_level += (frame_reference - _normalization_level) * norm_alpha;
            }
            _normalization_level = std::clamp(_normalization_level, 60.0f, 32768.0f);
        } else {
            _normalization_level *= 0.90f;
            if (_normalization_level < 60.0f) {
                _normalization_level = 0.0f;
            }
        }

        Frame frame{};
        for (std::size_t index = 0; index < frame.size(); ++index) {
            float normalized = 0.0f;
            if (_normalization_level > 0.0f) {
                const float ratio =
                    std::clamp(raw_levels[index] / _normalization_level, 0.0f, 1.0f);
                normalized = ratio * (2.20f - 1.20f * ratio);
                if (normalized < 0.035f) {
                    normalized = 0.0f;
                }
            }
            const float smooth_alpha = normalized > _display_levels[index] ? 0.60f : 0.36f;
            _display_levels[index] += (normalized - _display_levels[index]) * smooth_alpha;
            frame[index] = static_cast<int16_t>(
                8.5f + 112.0f * std::clamp(_display_levels[index], 0.0f, 1.0f));
        }
        return frame;
    }

private:
    float _noise_floor = 0.0f;
    std::array<float, level_count> _display_levels{};
    float _normalization_level = 0.0f;
};
