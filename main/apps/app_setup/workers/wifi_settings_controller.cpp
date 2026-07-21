/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "wifi_settings_controller.h"

#ifndef STOPWATCH_HOST_TEST
#include "runtime/services/shared_wifi_service.h"

#include <ssid_manager.h>
#include <wifi_manager.h>
#endif

#include <utility>

namespace {

#ifndef STOPWATCH_HOST_TEST
class ProductionWifiSettingsBackend final : public WifiSettingsBackend {
public:
    ProductionWifiSettingsBackend()
        : _wifi_service(GetSharedWifiService()), _wifi_manager(WifiManager::GetInstance()),
          _ssid_manager(SsidManager::GetInstance())
    {
    }

    std::vector<std::string> copySavedSsids() const override
    {
        std::vector<std::string> ssids;
        const auto& saved = _ssid_manager.GetSsidList();
        ssids.reserve(saved.size());
        for (const auto& item : saved) {
            ssids.push_back(item.ssid);
        }
        return ssids;
    }

    bool selectSavedSsid(std::size_t index) override
    {
        const auto& saved = _ssid_manager.GetSsidList();
        if (index >= saved.size()) {
            return false;
        }
        _ssid_manager.SetDefaultSsid(static_cast<int>(index));
        return true;
    }

    bool acquireSta() override
    {
        if (!_sta_lease.valid()) {
            _sta_lease = _wifi_service.acquireSta(RuntimeOwner::Settings);
        }
        return _sta_lease.valid();
    }

    void releaseSta() override
    {
        _sta_lease.reset();
    }

    bool staHeld() const override
    {
        return _sta_lease.valid();
    }

    bool reconnectSta() override
    {
        return _wifi_service.reconnectSta(RuntimeOwner::Settings);
    }

    bool acquireConfigAp() override
    {
        if (!_config_ap_session.valid()) {
            _config_ap_session = _wifi_service.acquireConfigAp(RuntimeOwner::Settings);
        }
        return _config_ap_session.valid();
    }

    void releaseConfigAp() override
    {
        _config_ap_session.reset();
    }

    bool configApHeld() const override
    {
        return _config_ap_session.valid();
    }

    bool configApMode() const override
    {
        return _wifi_manager.IsConfigMode();
    }

    std::string ssid() const override
    {
        return _wifi_manager.GetSsid();
    }

    bool isConnected() const override
    {
        return _wifi_manager.IsConnected();
    }

    std::string ipAddress() const override
    {
        return _wifi_manager.GetIpAddress();
    }

    std::string apSsid() const override
    {
        return _wifi_manager.GetApSsid();
    }

    std::string apUrl() const override
    {
        return _wifi_manager.GetApWebUrl();
    }

    int rssi() const override
    {
        return _wifi_manager.GetRssi();
    }

private:
    SharedWifiService& _wifi_service;
    WifiManager& _wifi_manager;
    SsidManager& _ssid_manager;
    WifiStaLease _sta_lease;
    WifiConfigApSession _config_ap_session;
};
#else
class HostWifiSettingsBackend final : public WifiSettingsBackend {
public:
    std::vector<std::string> copySavedSsids() const override { return {}; }
    bool selectSavedSsid(std::size_t) override { return false; }
    bool acquireSta() override { return false; }
    void releaseSta() override {}
    bool staHeld() const override { return false; }
    bool reconnectSta() override { return false; }
    bool acquireConfigAp() override { return false; }
    void releaseConfigAp() override {}
    bool configApHeld() const override { return false; }
    bool configApMode() const override { return false; }
    std::string ssid() const override { return {}; }
    bool isConnected() const override { return false; }
    std::string ipAddress() const override { return {}; }
    std::string apSsid() const override { return {}; }
    std::string apUrl() const override { return {}; }
    int rssi() const override { return 0; }
};
#endif

std::unique_ptr<WifiSettingsBackend> makeDefaultWifiSettingsBackend()
{
#ifndef STOPWATCH_HOST_TEST
    return std::make_unique<ProductionWifiSettingsBackend>();
#else
    return std::make_unique<HostWifiSettingsBackend>();
#endif
}

} // namespace

WifiSettingsController::WifiSettingsController()
    : WifiSettingsController(makeDefaultWifiSettingsBackend())
{
}

WifiSettingsController::WifiSettingsController(std::unique_ptr<WifiSettingsBackend> backend)
    : _backend(std::move(backend))
{
    refreshSavedSsids();
    acquireStaWhenSaved();
}

WifiSettingsController::~WifiSettingsController()
{
    _backend->releaseConfigAp();
    _backend->releaseSta();
}

bool WifiSettingsController::startPairing()
{
    _pairing_failed = false;
    if (_backend->configApHeld()) {
        return true;
    }

    if (_backend->staHeld()) {
        _backend->releaseSta();
    }

    if (_backend->acquireConfigAp()) {
        return true;
    }

    _pairing_failed = true;
    acquireStaWhenSaved();
    return false;
}

void WifiSettingsController::stopPairing()
{
    const bool had_config_ap = _backend->configApHeld();
    _pairing_failed = false;
    if (had_config_ap) {
        _backend->releaseConfigAp();
        finishPairing();
    }
}

bool WifiSettingsController::selectSaved(std::size_t index)
{
    if (index >= _saved_ssids.size() || !_backend->selectSavedSsid(index)) {
        return false;
    }

    refreshSavedSsids();
    stopPairing();
    if (!_backend->staHeld()) {
        acquireStaWhenSaved();
    }
    return _backend->staHeld() && _backend->reconnectSta();
}

WifiSettingsSnapshot WifiSettingsController::poll()
{
    if (_backend->configApHeld() && !_backend->configApMode()) {
        _backend->releaseConfigAp();
        finishPairing();
    }

    WifiSettingsSnapshot snapshot;
    snapshot.saved_ssids = _saved_ssids;

    if (_backend->configApHeld()) {
        snapshot.state = WifiSettingsState::Pairing;
        snapshot.ap_ssid = _backend->apSsid();
        snapshot.ap_url = _backend->apUrl();
        return snapshot;
    }

    if (_pairing_failed) {
        snapshot.state = WifiSettingsState::PairingFailed;
        return snapshot;
    }

    if (_saved_ssids.empty()) {
        snapshot.state = WifiSettingsState::NoSaved;
        return snapshot;
    }

    if (!_backend->staHeld()) {
        snapshot.state = WifiSettingsState::Busy;
        return snapshot;
    }

    snapshot.ssid = _backend->ssid();
    if (_backend->isConnected()) {
        snapshot.state = WifiSettingsState::Connected;
        snapshot.ip_address = _backend->ipAddress();
        snapshot.rssi = _backend->rssi();
    } else if (!snapshot.ssid.empty()) {
        snapshot.state = WifiSettingsState::Connecting;
    } else {
        snapshot.state = WifiSettingsState::NotConnected;
    }
    return snapshot;
}

void WifiSettingsController::refreshSavedSsids()
{
    _saved_ssids = _backend->copySavedSsids();
}

void WifiSettingsController::acquireStaWhenSaved()
{
    if (!_saved_ssids.empty() && !_backend->staHeld()) {
        _backend->acquireSta();
    }
}

void WifiSettingsController::finishPairing()
{
    refreshSavedSsids();
    acquireStaWhenSaved();
}
