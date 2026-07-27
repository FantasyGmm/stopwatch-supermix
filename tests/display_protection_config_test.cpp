#include "runtime/display/display_protection_config.h"

#include <array>
#include <cassert>
#include <string_view>

int main()
{
    const auto defaults = defaultDisplayProtectionConfig();
    assert(defaults.dim_choice == 1);
    assert(defaults.screen_off_choice == 1);
    assert(!defaults.protect_while_charging);
    assert(defaults.dim_enabled);
    assert(defaults.screen_off_enabled);
    assert(displayProtectionDimMs(defaults) == 15000U);
    assert(displayProtectionScreenOffMs(defaults) == 60000U);
    assert(displayProtectionDimLabel(5) == "5m");
    assert(displayProtectionScreenOffLabel(4) == "10m");

    constexpr std::array<uint32_t, DisplayProtectionDimChoiceCount> dim_ms{
        10000U, 15000U, 30000U, 60000U, 120000U, 300000U};
    constexpr std::array<std::string_view, DisplayProtectionDimChoiceCount>
        dim_labels{"10s", "15s", "30s", "1m", "2m", "5m"};
    for (size_t choice = 0; choice < dim_ms.size(); ++choice) {
        const DisplayProtectionConfig config{static_cast<uint8_t>(choice), 4,
                                             false};
        assert(displayProtectionDimMs(config) == dim_ms[choice]);
        assert(displayProtectionDimLabel(static_cast<int>(choice)) ==
               dim_labels[choice]);
    }

    constexpr std::array<uint32_t, DisplayProtectionScreenOffChoiceCount>
        off_ms{30000U, 60000U, 120000U, 300000U, 600000U};
    constexpr std::array<std::string_view,
                         DisplayProtectionScreenOffChoiceCount>
        off_labels{"30s", "1m", "2m", "5m", "10m"};
    for (size_t choice = 0; choice < off_ms.size(); ++choice) {
        const DisplayProtectionConfig config{0, static_cast<uint8_t>(choice),
                                             false};
        assert(displayProtectionScreenOffMs(config) == off_ms[choice]);
        assert(displayProtectionScreenOffLabel(static_cast<int>(choice)) ==
               off_labels[choice]);
    }

    DisplayProtectionConfig invalid{99, 0, true};
    const auto repaired = sanitizeDisplayProtectionConfig(invalid);
    assert(repaired.dim_choice == 5);
    assert(repaired.screen_off_choice == 4);
    assert(repaired.protect_while_charging);

    auto dim_disabled = defaults;
    dim_disabled.dim_enabled = false;
    dim_disabled.dim_choice = 5;
    dim_disabled.screen_off_choice = 0;
    const auto preserved_direct_off = sanitizeDisplayProtectionConfig(dim_disabled);
    assert(!preserved_direct_off.dim_enabled);
    assert(preserved_direct_off.screen_off_enabled);
    assert(preserved_direct_off.dim_choice == 5);
    assert(preserved_direct_off.screen_off_choice == 0);

    auto off_disabled = defaults;
    off_disabled.screen_off_enabled = false;
    off_disabled.dim_choice = 5;
    off_disabled.screen_off_choice = 0;
    const auto preserved_dim_only = sanitizeDisplayProtectionConfig(off_disabled);
    assert(preserved_dim_only.dim_enabled);
    assert(!preserved_dim_only.screen_off_enabled);
    assert(preserved_dim_only.dim_choice == 5);
    assert(preserved_dim_only.screen_off_choice == 0);

    auto both_enabled_invalid = defaults;
    both_enabled_invalid.dim_choice = 5;
    both_enabled_invalid.screen_off_choice = 0;
    const auto repaired_enabled = sanitizeDisplayProtectionConfig(both_enabled_invalid);
    assert(repaired_enabled.screen_off_choice == 4);

    auto linked = withDisplayProtectionDimChoice(defaults, 4);
    assert(linked.dim_choice == 4);
    assert(linked.screen_off_choice == 3);
    linked = withDisplayProtectionDimChoice(linked, 5);
    assert(linked.screen_off_choice == 4);

    const auto dim_preserving_off = withDisplayProtectionDimChoice(
        DisplayProtectionConfig{1, 3, false}, 2);
    assert(dim_preserving_off.dim_choice == 2);
    assert(dim_preserving_off.screen_off_choice == 3);

    linked = withDisplayProtectionScreenOffChoice(defaults, 0);
    assert(linked.screen_off_choice == 0);
    assert(linked.dim_choice == 1);

    const auto off_preserving_dim = withDisplayProtectionScreenOffChoice(
        DisplayProtectionConfig{0, 1, false}, 3);
    assert(off_preserving_dim.screen_off_choice == 3);
    assert(off_preserving_dim.dim_choice == 0);
    return 0;
}
