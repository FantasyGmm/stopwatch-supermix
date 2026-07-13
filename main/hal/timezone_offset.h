#pragma once

#include <optional>
#include <string>

std::optional<std::string> posix_timezone_for_utc_offset(int offset_minutes);
