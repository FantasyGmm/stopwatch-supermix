#include "display_protection_config.h"

#include <array>

namespace {

constexpr std::array<uint32_t, DisplayProtectionDimChoiceCount> kDimMs{
    10000U, 15000U, 30000U, 60000U, 120000U, 300000U};
constexpr std::array<std::string_view, DisplayProtectionDimChoiceCount> kDimLabels{
    "10s", "15s", "30s", "1m", "2m", "5m"};
constexpr std::array<uint32_t, DisplayProtectionScreenOffChoiceCount> kOffMs{
    30000U, 60000U, 120000U, 300000U, 600000U};
constexpr std::array<std::string_view, DisplayProtectionScreenOffChoiceCount> kOffLabels{
    "30s", "1m", "2m", "5m", "10m"};

template <typename T, size_t Count>
uint8_t clampedChoice(int choice, const std::array<T, Count>&)
{
    if (choice < 0) {
        return 0;
    }
    if (choice >= static_cast<int>(Count)) {
        return static_cast<uint8_t>(Count - 1);
    }
    return static_cast<uint8_t>(choice);
}

uint8_t firstScreenOffChoiceAfter(uint32_t dim_ms)
{
    for (uint8_t choice = 0; choice < DisplayProtectionScreenOffChoiceCount; ++choice) {
        if (kOffMs[choice] > dim_ms) {
            return choice;
        }
    }
    return DisplayProtectionScreenOffChoiceCount - 1;
}

uint8_t lastDimChoiceBefore(uint32_t screen_off_ms)
{
    for (uint8_t choice = DisplayProtectionDimChoiceCount; choice > 0; --choice) {
        const auto candidate = static_cast<uint8_t>(choice - 1);
        if (kDimMs[candidate] < screen_off_ms) {
            return candidate;
        }
    }
    return 0;
}

} // namespace

DisplayProtectionConfig defaultDisplayProtectionConfig()
{
    return {};
}

DisplayProtectionConfig sanitizeDisplayProtectionConfig(DisplayProtectionConfig config)
{
    config.dim_choice = clampedChoice(config.dim_choice, kDimMs);
    config.screen_off_choice = clampedChoice(config.screen_off_choice, kOffMs);
    const auto dim_ms = kDimMs[config.dim_choice];
    if (config.dim_enabled && config.screen_off_enabled &&
        kOffMs[config.screen_off_choice] <= dim_ms) {
        config.screen_off_choice = firstScreenOffChoiceAfter(dim_ms);
    }
    return config;
}

DisplayProtectionConfig withDisplayProtectionDimChoice(DisplayProtectionConfig config,
                                                       int choice)
{
    config.dim_choice = clampedChoice(choice, kDimMs);
    config.screen_off_choice = clampedChoice(config.screen_off_choice, kOffMs);
    if (config.dim_enabled && config.screen_off_enabled &&
        kOffMs[config.screen_off_choice] <= kDimMs[config.dim_choice]) {
        config.screen_off_choice = firstScreenOffChoiceAfter(kDimMs[config.dim_choice]);
    }
    return config;
}

DisplayProtectionConfig withDisplayProtectionScreenOffChoice(DisplayProtectionConfig config,
                                                             int choice)
{
    config.screen_off_choice = clampedChoice(choice, kOffMs);
    config.dim_choice = clampedChoice(config.dim_choice, kDimMs);
    if (config.dim_enabled && config.screen_off_enabled &&
        kDimMs[config.dim_choice] >= kOffMs[config.screen_off_choice]) {
        config.dim_choice = lastDimChoiceBefore(kOffMs[config.screen_off_choice]);
    }
    return config;
}

uint32_t displayProtectionDimMs(const DisplayProtectionConfig& config)
{
    return kDimMs[clampedChoice(config.dim_choice, kDimMs)];
}

uint32_t displayProtectionScreenOffMs(const DisplayProtectionConfig& config)
{
    return kOffMs[clampedChoice(config.screen_off_choice, kOffMs)];
}

std::string_view displayProtectionDimLabel(int choice)
{
    return kDimLabels[clampedChoice(choice, kDimLabels)];
}

std::string_view displayProtectionScreenOffLabel(int choice)
{
    return kOffLabels[clampedChoice(choice, kOffLabels)];
}
