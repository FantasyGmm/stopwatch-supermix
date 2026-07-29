#include "main/apps/app_codex_status/codex_status_priority.h"

#include <cassert>
#include <string_view>

int main()
{
    constexpr uint32_t dim = 0x777777;
    constexpr uint32_t blue = 0x0066ff;
    constexpr uint32_t red = 0xff0000;

    auto status = resolve_codex_status("BLE LIVE", blue, false, false, false,
                                       blue, red, dim);
    assert(std::string_view(status.text) == "BLE LIVE");
    assert(status.color == blue);

    status = resolve_codex_status("BLE WAIT", dim, false, false, false,
                                  blue, red, dim);
    assert(std::string_view(status.text) == "BLE WAIT");

    status = resolve_codex_status("BLE LIVE", blue, false, true, false,
                                  blue, red, dim);
    assert(std::string_view(status.text) == "VOICE");
    assert(status.color == blue);

    status = resolve_codex_status("BLE ERR", red, true, true, true,
                                  blue, red, dim);
    assert(std::string_view(status.text) == "BLE ERR");
    assert(status.color == red);

    status = resolve_codex_status("LIVE", blue, false, false, true,
                                  blue, red, dim);
    assert(std::string_view(status.text) == "VOICE ERR");
    assert(status.color == red);

    status = resolve_codex_status("", dim, false, false, false,
                                  blue, red, dim);
    assert(std::string_view(status.text) == "VOICE OFF");
    assert(status.color == dim);
}
