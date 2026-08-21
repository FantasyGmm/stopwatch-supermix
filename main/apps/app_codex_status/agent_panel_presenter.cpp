#include "agent_panel_presenter.h"

#include <cstdio>
#include <ctime>

namespace agent_panel_presentation {

namespace {

constexpr int64_t kMinuteMilliseconds = 60LL * 1000LL;
constexpr int64_t kHourMilliseconds = 60LL * kMinuteMilliseconds;
constexpr int64_t kDayMilliseconds = 24LL * kHourMilliseconds;

void copy_text(char* output, size_t output_size, const char* text)
{
    if (output == nullptr || output_size == 0) {
        return;
    }
    std::snprintf(output, output_size, "%s", text);
}

}  // namespace

void format_last_connected(
    AgentPanelRowState state,
    int64_t last_connected_at,
    int64_t now,
    char* output,
    size_t output_size)
{
    if (output == nullptr || output_size == 0) {
        return;
    }
    if (state == AgentPanelRowState::Connected) {
        copy_text(output, output_size, "Now");
        return;
    }
    if (state == AgentPanelRowState::Requesting) {
        copy_text(output, output_size, "Awaiting");
        return;
    }
    if (last_connected_at < kMinimumUnixMilliseconds ||
        now < kMinimumUnixMilliseconds || last_connected_at > now) {
        copy_text(output, output_size, "Unknown");
        return;
    }

    const int64_t elapsed = now - last_connected_at;
    if (elapsed < kMinuteMilliseconds) {
        copy_text(output, output_size, "Now");
    } else if (elapsed < kHourMilliseconds) {
        std::snprintf(
            output,
            output_size,
            "%lld min",
            static_cast<long long>(elapsed / kMinuteMilliseconds));
    } else if (elapsed < kDayMilliseconds) {
        std::snprintf(
            output,
            output_size,
            "%lld hr",
            static_cast<long long>(elapsed / kHourMilliseconds));
    } else if (elapsed < 2LL * kDayMilliseconds) {
        copy_text(output, output_size, "Yesterday");
    } else if (elapsed < 7LL * kDayMilliseconds) {
        const std::time_t seconds =
            static_cast<std::time_t>(last_connected_at / 1000LL);
        std::tm local = {};
        if (localtime_r(&seconds, &local) == nullptr) {
            copy_text(output, output_size, "Unknown");
            return;
        }
        static constexpr const char* kWeekdays[] = {
            "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        copy_text(output, output_size, kWeekdays[local.tm_wday]);
    } else if (elapsed < 30LL * kDayMilliseconds) {
        std::snprintf(
            output,
            output_size,
            "%lld days",
            static_cast<long long>(elapsed / kDayMilliseconds));
    } else {
        const std::time_t seconds =
            static_cast<std::time_t>(last_connected_at / 1000LL);
        std::tm local = {};
        if (localtime_r(&seconds, &local) == nullptr) {
            copy_text(output, output_size, "Unknown");
            return;
        }
        std::snprintf(
            output,
            output_size,
            "%02d/%02d",
            local.tm_mon + 1,
            local.tm_mday);
    }
}

}  // namespace agent_panel_presentation
