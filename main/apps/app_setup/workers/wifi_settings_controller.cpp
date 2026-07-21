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

    WifiSettingsSwitchResult selectSavedSsidAndReconnect(std::size_t index) override
    {
        const auto& saved = _ssid_manager.GetSsidList();
        if (index >= saved.size()) {
            return WifiSettingsSwitchResult::Busy;
        }

        const auto result = _wifi_service.reconfigureSta(
            RuntimeOwner::Settings,
            [this, index]() { _ssid_manager.SetDefaultSsid(static_cast<int>(index)); });
        switch (result) {
        case StaReconfigureResult::Applied:
            return WifiSettingsSwitchResult::Applied;
        case StaReconfigureResult::Busy:
            return WifiSettingsSwitchResult::Busy;
        case StaReconfigureResult::StartFailed:
            return WifiSettingsSwitchResult::StartFailed;
        }
        return WifiSettingsSwitchResult::StartFailed;
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

    WifiSettingsConfigApResult acquireConfigAp() override
    {
        if (_config_ap_session.valid()) {
            return WifiSettingsConfigApResult::Acquired;
        }

        auto acquired = _wifi_service.acquireConfigApDetailed(RuntimeOwner::Settings);
        switch (acquired.result) {
        case WifiConfigApAcquireResult::Acquired:
            _config_ap_session = std::move(acquired.session);
            return WifiSettingsConfigApResult::Acquired;
        case WifiConfigApAcquireResult::Busy:
            return WifiSettingsConfigApResult::Busy;
        case WifiConfigApAcquireResult::StartFailed:
            return WifiSettingsConfigApResult::StartFailed;
        }
        return WifiSettingsConfigApResult::StartFailed;
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
    WifiSettingsSwitchResult selectSavedSsidAndReconnect(std::size_t) override
    {
        return WifiSettingsSwitchResult::Busy;
    }
    bool acquireSta() override { return false; }
    void releaseSta() override {}
    bool staHeld() const override { return false; }
    WifiSettingsConfigApResult acquireConfigAp() override
    {
        return WifiSettingsConfigApResult::StartFailed;
    }
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
    _pairing_result = WifiSettingsConfigApResult::Acquired;
    _switch_result = WifiSettingsSwitchResult::Applied;
    if (_backend->configApHeld()) {
        return true;
    }

    if (_backend->staHeld()) {
        _backend->releaseSta();
    }

    const WifiSettingsConfigApResult result = _backend->acquireConfigAp();
    if (result == WifiSettingsConfigApResult::Acquired) {
        return true;
    }

    _pairing_result = result;
    acquireStaWhenSaved();
    return false;
}

void WifiSettingsController::stopPairing()
{
    const bool had_config_ap = _backend->configApHeld();
    _pairing_result = WifiSettingsConfigApResult::Acquired;
    _switch_result = WifiSettingsSwitchResult::Applied;
    if (had_config_ap) {
        _backend->releaseConfigAp();
        finishPairing();
    }
}

bool WifiSettingsController::selectSaved(std::size_t index)
{
    if (index >= _saved_ssids.size()) {
        return false;
    }

    if (_backend->configApHeld() || !_backend->staHeld()) {
        return false;
    }

    _pairing_result = WifiSettingsConfigApResult::Acquired;
    _switch_result = _backend->selectSavedSsidAndReconnect(index);
    if (_switch_result != WifiSettingsSwitchResult::Busy) {
        refreshSavedSsids();
    }
    return _switch_result == WifiSettingsSwitchResult::Applied;
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

    if (_pairing_result == WifiSettingsConfigApResult::Busy) {
        snapshot.state = WifiSettingsState::Busy;
        return snapshot;
    }

    if (_pairing_result == WifiSettingsConfigApResult::StartFailed) {
        snapshot.state = WifiSettingsState::PairingFailed;
        return snapshot;
    }

    if (_switch_result == WifiSettingsSwitchResult::Busy) {
        snapshot.state = WifiSettingsState::Busy;
        return snapshot;
    }

    if (_switch_result == WifiSettingsSwitchResult::StartFailed) {
        snapshot.state = WifiSettingsState::NotConnected;
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
    _switch_result = WifiSettingsSwitchResult::Applied;
    refreshSavedSsids();
    acquireStaWhenSaved();
}
