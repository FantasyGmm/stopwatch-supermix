/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string>
#include <vector>

enum class WifiSettingsState {
    NoSaved,
    NotConnected,
    Connecting,
    Connected,
    Pairing,
    Busy,
    PairingFailed,
};

struct WifiSettingsSnapshot {
    WifiSettingsState state = WifiSettingsState::NoSaved;
    std::vector<std::string> saved_ssids;
    std::string ssid;
    std::string ip_address;
    std::string ap_ssid;
    std::string ap_url;
    int rssi = 0;

    bool operator==(const WifiSettingsSnapshot& other) const
    {
        return state == other.state && saved_ssids == other.saved_ssids && ssid == other.ssid &&
               ip_address == other.ip_address && ap_ssid == other.ap_ssid && ap_url == other.ap_url &&
               rssi == other.rssi;
    }

    bool operator!=(const WifiSettingsSnapshot& other) const
    {
        return !(*this == other);
    }
};
