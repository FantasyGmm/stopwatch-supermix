/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstddef>
#include <string_view>

namespace stopwatch::wifi {

template <typename SavedRange, typename SavedSsidAccessor>
std::size_t savedSsidPriority(std::string_view candidate_ssid, const SavedRange& saved,
                              const SavedSsidAccessor& saved_ssid)
{
    std::size_t index = 0;
    for (const auto& item : saved) {
        if (candidate_ssid == saved_ssid(item)) {
            return index;
        }
        ++index;
    }
    return index;
}

template <typename SavedRange, typename SavedSsidAccessor>
bool savedCandidatePriorityLess(std::string_view left_ssid, int left_rssi,
                                std::string_view right_ssid, int right_rssi,
                                const SavedRange& saved,
                                const SavedSsidAccessor& saved_ssid)
{
    const auto left_priority = savedSsidPriority(left_ssid, saved, saved_ssid);
    const auto right_priority = savedSsidPriority(right_ssid, saved, saved_ssid);
    return left_priority != right_priority ? left_priority < right_priority
                                           : left_rssi > right_rssi;
}

} // namespace stopwatch::wifi
