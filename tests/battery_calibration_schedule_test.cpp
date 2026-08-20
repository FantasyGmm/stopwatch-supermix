#include "runtime/power/battery_calibration_schedule.h"

#include <cassert>
#include <array>
#include <cstdint>
#include <string_view>

namespace {

BatteryCalibrationInput sample(uint32_t now_ms, uint32_t revision)
{
    return {
        .now_ms = now_ms,
        .revision = revision,
        .raw_mv = 4012,
        .filtered_mv = 3998,
        .percent = 77,
        .external_power = false,
    };
}

}  // namespace

int main()
{
    BatteryCalibrationSchedule schedule;
    assert(!schedule.active());

    schedule.start(1000);
    assert(schedule.active());

    const auto initial = schedule.update(sample(1000, 1));
    assert(initial.append);
    assert(initial.elapsed_seconds == 0);
    assert(schedule.sampleCount() == 1);

    assert(!schedule.update(sample(31000, 2)).append);
    const auto minute = schedule.update(sample(61000, 3));
    assert(minute.append);
    assert(minute.elapsed_seconds == 60);
    assert(schedule.sampleCount() == 2);

    assert(!schedule.update(sample(121000, 3)).append);
    const auto next_revision = schedule.update(sample(121001, 4));
    assert(next_revision.append);
    assert(next_revision.elapsed_seconds == 120);

    schedule.stop();
    assert(!schedule.active());
    assert(!schedule.update(sample(181000, 5)).append);

    BatteryCalibrationSchedule wrapping;
    wrapping.start(UINT32_MAX - 30000U);
    assert(wrapping.update(sample(UINT32_MAX - 30000U, 1)).append);
    assert(!wrapping.update(sample(29998U, 2)).append);
    const auto wrapped_minute = wrapping.update(sample(29999U, 3));
    assert(wrapped_minute.append);
    assert(wrapped_minute.elapsed_seconds == 60);

    std::array<char, 48> line{};
    assert(FormatBatteryCalibrationCsvLine(sample(61000, 3), 60, line.data(), line.size()));
    assert(std::string_view(line.data()) == "60,4012,3998,77,0\n");
    std::array<char, 8> short_line{};
    assert(!FormatBatteryCalibrationCsvLine(sample(61000, 3), 60, short_line.data(), short_line.size()));
    return 0;
}
