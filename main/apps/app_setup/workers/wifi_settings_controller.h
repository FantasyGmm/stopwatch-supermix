/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "wifi_settings_model.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class WifiSettingsBackend {
public:
    virtual ~WifiSettingsBackend() = default;

    virtual std::vector<std::string> copySavedSsids() const = 0;
    virtual bool selectSavedSsid(std::size_t index) = 0;

    virtual bool acquireSta() = 0;
    virtual void releaseSta() = 0;
    virtual bool staHeld() const = 0;
    virtual bool reconnectSta() = 0;

    virtual bool acquireConfigAp() = 0;
    virtual void releaseConfigAp() = 0;
    virtual bool configApHeld() const = 0;
    virtual bool configApMode() const = 0;

    virtual std::string ssid() const = 0;
    virtual bool isConnected() const = 0;
    virtual std::string ipAddress() const = 0;
    virtual std::string apSsid() const = 0;
    virtual std::string apUrl() const = 0;
    virtual int rssi() const = 0;
};

class WifiSettingsController {
public:
    WifiSettingsController();
    explicit WifiSettingsController(std::unique_ptr<WifiSettingsBackend> backend);
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
    void finishPairing();

    std::unique_ptr<WifiSettingsBackend> _backend;
    std::vector<std::string> _saved_ssids;
    bool _pairing_failed = false;
};
