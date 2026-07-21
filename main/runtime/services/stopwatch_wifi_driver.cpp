/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "shared_wifi_service.h"

#include <ssid_manager.h>
#include <wifi_manager.h>

namespace {
class StopWatchWifiDriver final : public SharedWifiDriver {
public:
    bool startSta() override
    {
        auto& wifi = WifiManager::GetInstance();
        if (!initialize(wifi)) {
            return false;
        }
        wifi.StartStation();
        wifi.SetPowerSaveLevel(WifiPowerSaveLevel::PERFORMANCE);
        return true;
    }

    void stopSta() override
    {
        auto& wifi = WifiManager::GetInstance();
        wifi.SetEventCallback(nullptr);
        wifi.StopConfigAp();
        wifi.StopStation();
    }

    bool startAp() override
    {
        auto& wifi = WifiManager::GetInstance();
        if (!initialize(wifi)) {
            return false;
        }
        wifi.SetEventCallback(nullptr);
        wifi.StopStation();
        wifi.StopConfigAp();
        return true;
    }

    void stopAp() override
    {
        // Badge owns the AP sockets and stops the raw AP before releasing its session.
    }

    bool startConfigAp() override
    {
        auto& wifi = WifiManager::GetInstance();
        if (!initialize(wifi)) {
            return false;
        }
        wifi.SetEventCallback(nullptr);
        wifi.StopStation();
        wifi.StartConfigAp();
        return wifi.IsConfigMode();
    }

    void stopConfigAp() override
    {
        WifiManager::GetInstance().StopConfigAp();
    }

    bool staRequiredBySystem() const override
    {
        return false;
    }

    bool staConnected() const override
    {
        return WifiManager::GetInstance().IsConnected();
    }

    bool hasSavedCredentials() const override
    {
        return !SsidManager::GetInstance().GetSsidList().empty();
    }

private:
    static bool initialize(WifiManager& wifi)
    {
        if (wifi.IsInitialized()) {
            return true;
        }
        WifiManagerConfig config;
        config.ssid_prefix = "StopWatch";
        config.language = "zh-CN";
        return wifi.Initialize(config);
    }
};

StopWatchWifiDriver wifi_driver;
SharedWifiPolicy wifi_policy(GetResourceSupervisor(), wifi_driver);
SharedWifiService wifi_service(wifi_policy);
} // namespace

SharedWifiService& GetSharedWifiService()
{
    return wifi_service;
}
