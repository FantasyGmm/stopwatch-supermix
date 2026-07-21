#include "apps/app_setup/workers/wifi_settings_model.h"

#include <cassert>
#include <string>
#include <utility>
#include <vector>

namespace {
WifiSettingsSnapshot snapshot(WifiSettingsState state)
{
    WifiSettingsSnapshot value;
    value.state = state;
    value.saved_ssids = {"Office", "Home"};
    value.ssid = "Office";
    value.ip_address = "192.168.1.25";
    value.ap_ssid = "StopWatch-1234";
    value.ap_url = "http://192.168.4.1";
    value.rssi = -55;
    return value;
}
} // namespace

int main()
{
    const std::vector<WifiSettingsState> states = {
        WifiSettingsState::NoSaved,
        WifiSettingsState::NotConnected,
        WifiSettingsState::Connecting,
        WifiSettingsState::Connected,
        WifiSettingsState::Pairing,
        WifiSettingsState::Busy,
        WifiSettingsState::PairingFailed,
    };

    for (const WifiSettingsState state : states) {
        auto value = snapshot(state);
        assert(value.state == state);
        assert(value.saved_ssids == std::vector<std::string>({"Office", "Home"}));
    }

    const auto connected = snapshot(WifiSettingsState::Connected);
    assert(connected == snapshot(WifiSettingsState::Connected));

    auto changed = connected;
    changed.ssid = "Home";
    assert(changed != connected);

    changed = connected;
    changed.ip_address = "192.168.1.26";
    assert(changed != connected);

    changed = connected;
    changed.ap_ssid = "StopWatch-5678";
    assert(changed != connected);

    changed = connected;
    changed.ap_url = "http://192.168.4.2";
    assert(changed != connected);

    changed = connected;
    changed.rssi = -56;
    assert(changed != connected);

    changed = connected;
    changed.saved_ssids.push_back("Guest");
    assert(changed != connected);

    return 0;
}
