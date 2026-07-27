#pragma once

#include <cstdint>
#include <string_view>

struct DisplayProtectionConfig {
    uint8_t dim_choice = 1;
    uint8_t screen_off_choice = 1;
    bool protect_while_charging = false;
    bool dim_enabled = true;
    bool screen_off_enabled = true;
};

constexpr uint8_t DisplayProtectionDimChoiceCount = 6;
constexpr uint8_t DisplayProtectionScreenOffChoiceCount = 5;

DisplayProtectionConfig defaultDisplayProtectionConfig();
DisplayProtectionConfig sanitizeDisplayProtectionConfig(DisplayProtectionConfig config);
DisplayProtectionConfig withDisplayProtectionDimChoice(DisplayProtectionConfig config,
                                                       int choice);
DisplayProtectionConfig withDisplayProtectionScreenOffChoice(DisplayProtectionConfig config,
                                                             int choice);
uint32_t displayProtectionDimMs(const DisplayProtectionConfig& config);
uint32_t displayProtectionScreenOffMs(const DisplayProtectionConfig& config);
std::string_view displayProtectionDimLabel(int choice);
std::string_view displayProtectionScreenOffLabel(int choice);
