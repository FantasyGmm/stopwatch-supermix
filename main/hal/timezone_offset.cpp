#include "timezone_offset.h"

#include <cstdio>
#include <cstdlib>

std::optional<std::string> posix_timezone_for_utc_offset(int offset_minutes)
{
    if (offset_minutes < -14 * 60 || offset_minutes > 14 * 60) {
        return std::nullopt;
    }

    const int absolute   = std::abs(offset_minutes);
    const int hours      = absolute / 60;
    const int minutes    = absolute % 60;
    const char label_sign = offset_minutes < 0 ? '-' : '+';
    char buffer[40]       = {};

    if (offset_minutes == 0) {
        std::snprintf(buffer, sizeof(buffer), "<UTC+0000>0");
    } else if (minutes == 0) {
        const char* posix_sign = offset_minutes > 0 ? "-" : "";
        std::snprintf(buffer, sizeof(buffer), "<UTC%c%02d%02d>%s%d", label_sign, hours, minutes, posix_sign, hours);
    } else {
        const char* posix_sign = offset_minutes > 0 ? "-" : "";
        std::snprintf(buffer, sizeof(buffer), "<UTC%c%02d%02d>%s%d:%02d", label_sign, hours, minutes, posix_sign,
                      hours, minutes);
    }
    return std::string(buffer);
}
