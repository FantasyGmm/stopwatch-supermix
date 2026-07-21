/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "runtime/services/shared_wifi_service.h"
#include "wifi_settings_model.h"

#include <cstddef>
#include <vector>

class SsidManager;
class WifiManager;

class WifiSettingsController {
public:
    WifiSettingsController();
    WifiSettingsController(SharedWifiService& wifi_service, WifiManager& wifi_manager, SsidManager& ssid_manager);
    ~WifiSettingsController();

    WifiSettingsController(const WifiSettingsController&) = delete;
    WifiSettingsController& operator=(const WifiSettingsController&) = delete;

    bool startPairing();
    void stopPairing();
    bool selectSaved(std::size_t index);
    WifiSettingsSnapshot poll();

private:
    void refreshSavedSsids();
    void acquireStaWhenSaved();

    SharedWifiService& _wifi_service;
    WifiManager& _wifi_manager;
    SsidManager& _ssid_manager;
    WifiStaLease _sta_lease;
    WifiConfigApSession _config_ap_session;
    std::vector<std::string> _saved_ssids;
    bool _pairing_failed = false;
    bool _refresh_after_pairing = false;
};
