/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "wifi_settings_controller.h"

#include <ssid_manager.h>
#include <wifi_manager.h>

#include <utility>

WifiSettingsController::WifiSettingsController()
    : WifiSettingsController(GetSharedWifiService(), WifiManager::GetInstance(), SsidManager::GetInstance())
{
}

WifiSettingsController::WifiSettingsController(
    SharedWifiService& wifi_service, WifiManager& wifi_manager, SsidManager& ssid_manager)
    : _wifi_service(wifi_service), _wifi_manager(wifi_manager), _ssid_manager(ssid_manager)
{
    refreshSavedSsids();
    acquireStaWhenSaved();
}

WifiSettingsController::~WifiSettingsController()
{
    stopPairing();
    _sta_lease.reset();
}

bool WifiSettingsController::startPairing()
{
    _pairing_failed = false;
    _refresh_after_pairing = false;
    _sta_lease.reset();
    _config_ap_session = _wifi_service.acquireConfigAp(RuntimeOwner::Settings);
    _pairing_failed = !_config_ap_session.valid();
    return !_pairing_failed;
}

void WifiSettingsController::stopPairing()
{
    _refresh_after_pairing = _config_ap_session.valid();
    _config_ap_session.reset();
    _pairing_failed = false;
}

bool WifiSettingsController::selectSaved(std::size_t index)
{
    const auto& saved = _ssid_manager.GetSsidList();
    if (index >= saved.size()) {
        return false;
    }

    _ssid_manager.SetDefaultSsid(static_cast<int>(index));
    refreshSavedSsids();
    stopPairing();

    if (!_sta_lease.valid()) {
        acquireStaWhenSaved();
        return _sta_lease.valid();
    }

    return _wifi_service.reconnectSta(RuntimeOwner::Settings);
}

WifiSettingsSnapshot WifiSettingsController::poll()
{
    if (_config_ap_session.valid() && !_wifi_manager.IsConfigMode()) {
        _config_ap_session.reset();
        _refresh_after_pairing = true;
    }

    if (_refresh_after_pairing) {
        refreshSavedSsids();
        acquireStaWhenSaved();
        _refresh_after_pairing = false;
    }

    WifiSettingsSnapshot snapshot;
    snapshot.saved_ssids = _saved_ssids;

    if (_config_ap_session.valid()) {
        snapshot.state = WifiSettingsState::Pairing;
        snapshot.ap_ssid = _wifi_manager.GetApSsid();
        snapshot.ap_url = _wifi_manager.GetApWebUrl();
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

    if (!_sta_lease.valid()) {
        snapshot.state = WifiSettingsState::Busy;
        return snapshot;
    }

    snapshot.ssid = _wifi_manager.GetSsid();
    if (_wifi_manager.IsConnected()) {
        snapshot.state = WifiSettingsState::Connected;
        snapshot.ip_address = _wifi_manager.GetIpAddress();
        snapshot.rssi = _wifi_manager.GetRssi();
    } else if (!snapshot.ssid.empty()) {
        snapshot.state = WifiSettingsState::Connecting;
    } else {
        snapshot.state = WifiSettingsState::NotConnected;
    }
    return snapshot;
}

void WifiSettingsController::refreshSavedSsids()
{
    _saved_ssids.clear();
    const auto& saved = _ssid_manager.GetSsidList();
    _saved_ssids.reserve(saved.size());
    for (const auto& item : saved) {
        _saved_ssids.push_back(item.ssid);
    }
}

void WifiSettingsController::acquireStaWhenSaved()
{
    if (!_saved_ssids.empty() && !_sta_lease.valid()) {
        _sta_lease = _wifi_service.acquireSta(RuntimeOwner::Settings);
    }
}
