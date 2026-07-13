#include "main/hal/timezone_offset.h"

#include <cassert>

int main()
{
    assert(posix_timezone_for_utc_offset(480).value() == "<UTC+0800>-8");
    assert(posix_timezone_for_utc_offset(-300).value() == "<UTC-0500>5");
    assert(posix_timezone_for_utc_offset(330).value() == "<UTC+0530>-5:30");
    assert(posix_timezone_for_utc_offset(345).value() == "<UTC+0545>-5:45");
    assert(posix_timezone_for_utc_offset(0).value() == "<UTC+0000>0");
    assert(!posix_timezone_for_utc_offset(841).has_value());
    assert(!posix_timezone_for_utc_offset(-841).has_value());
}
