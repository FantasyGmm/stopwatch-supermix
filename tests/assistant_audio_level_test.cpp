#include "apps/app_assistant/view/assistant_input_waveform.h"
#include "hal/audio/pcm_level.h"

#include <array>
#include <cassert>
#include <cstdint>

int main()
{
    constexpr std::array<int16_t, 8> silence{};
    constexpr std::array<int16_t, 8> quiet{
        -300, 300, -500, 500, -700, 700, -900, 900,
    };
    constexpr std::array<int16_t, 8> loud{
        -4000, 4000, -6000, 6000, -8000, 8000, -10000, 10000,
    };
    constexpr std::array<int16_t, 2> limits{INT16_MIN, INT16_MAX};

    assert(MeasurePcmMeanAbsoluteLevel(nullptr, 0) == 0);
    assert(MeasurePcmMeanAbsoluteLevel(silence.data(), silence.size()) == 0);
    assert(MeasurePcmMeanAbsoluteLevel(quiet.data(), quiet.size()) == 600);
    assert(MeasurePcmMeanAbsoluteLevel(loud.data(), loud.size()) == 7000);
    assert(MeasurePcmMeanAbsoluteLevel(limits.data(), limits.size()) == 32767);

    constexpr std::array<int16_t, 30> sampled_levels{
        -100, 100, -200, 200, -300, 300, -400, 400, -500, 500,
        -600, 600, -700, 700, -800, 800, -900, 900, -1000, 1000,
        -1100, 1100, -1200, 1200, -1300, 1300, -1400, 1400, -1500, 1500,
    };
    assert(MeasurePcmSparseLevel<64>(sampled_levels.data(), sampled_levels.size()) == 1164);

    constexpr std::array<uint16_t, 5> partial_history{100, 200, 300, 0, 0};
    assert((OrderPcmLevelHistory<5>(partial_history, 3) ==
            std::array<uint16_t, 5>{0, 0, 100, 200, 300}));
    constexpr std::array<uint16_t, 5> wrapped_history{600, 700, 300, 400, 500};
    assert((OrderPcmLevelHistory<5>(wrapped_history, 7) ==
            std::array<uint16_t, 5>{300, 400, 500, 600, 700}));

    AssistantInputWaveform waveform;
    const AssistantInputWaveform::InputLevels silent_levels{};
    const AssistantInputWaveform::InputLevels voice_levels{
        0, 200, 500, 900, 1500, 2400, 4200, 7000,
        3600, 2200, 1300, 700, 350, 100, 0,
    };
    const auto idle = waveform.update(silent_levels);
    const auto active = waveform.update(voice_levels);
    assert(active[7] > idle[7]);
    assert(active[7] > active[6]);
    assert(active[6] > active[5]);
    assert(active[8] > active[9]);
    assert(active[9] > active[10]);
    assert(active[0] == idle[0]);
    assert(active[14] == idle[14]);

    const auto decaying = waveform.update(silent_levels);
    assert(decaying[7] < active[7]);
    assert(decaying[7] > idle[7]);
    waveform.reset();
    assert(waveform.update(silent_levels) == idle);
    return 0;
}
