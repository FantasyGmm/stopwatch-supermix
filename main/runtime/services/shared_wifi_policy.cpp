/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "shared_wifi_policy.h"

SharedWifiPolicy::SharedWifiPolicy(ResourceSupervisor& resources, SharedWifiDriver& driver)
    : _resources(resources), _driver(driver)
{
}

bool SharedWifiPolicy::acquireSta(RuntimeOwner owner)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AcquireResult result = _resources.acquireWifi(owner);
    if (result != AcquireResult::Granted) {
        return false;
    }

    if (_resources.wifiLeaseCount() == 1 && !_driver.startSta()) {
        _resources.releaseWifi(owner);
        return false;
    }
    return true;
}

WifiConfiguredStaAcquireResult SharedWifiPolicy::acquireConfiguredSta(RuntimeOwner owner)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_resources.snapshot().wifi_ap_active) {
        return WifiConfiguredStaAcquireResult::Busy;
    }
    if (!_driver.hasSavedCredentials()) {
        return WifiConfiguredStaAcquireResult::NoCredentials;
    }

    const AcquireResult result = _resources.acquireWifi(owner);
    if (result != AcquireResult::Granted) {
        return WifiConfiguredStaAcquireResult::Busy;
    }
    if (_resources.wifiLeaseCount() == 1 && !_driver.startSta()) {
        _resources.releaseWifi(owner);
        return WifiConfiguredStaAcquireResult::StartFailed;
    }
    return WifiConfiguredStaAcquireResult::Acquired;
}

void SharedWifiPolicy::releaseSta(RuntimeOwner owner)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_resources.releaseWifi(owner) == ReleaseResult::LastOwnerReleased &&
        !_driver.staRequiredBySystem()) {
        _driver.stopSta();
    }
}

bool SharedWifiPolicy::acquireAp(RuntimeOwner owner)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AcquireResult result = _resources.acquireWifiAp(owner);
    if (result != AcquireResult::Granted) {
        return false;
    }

    if (!_driver.startAp()) {
        _resources.releaseWifiAp(owner);
        return false;
    }
    return true;
}

void SharedWifiPolicy::releaseAp(RuntimeOwner owner)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_resources.releaseWifiAp(owner) == ReleaseResult::LastOwnerReleased) {
        _driver.stopAp();
    }
}

WifiConfigApAcquireResult SharedWifiPolicy::acquireConfigApDetailed(RuntimeOwner owner)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AcquireResult result = _resources.acquireWifiAp(owner);
    if (result != AcquireResult::Granted) {
        return WifiConfigApAcquireResult::Busy;
    }

    if (!_driver.startConfigAp()) {
        _resources.releaseWifiAp(owner);
        return WifiConfigApAcquireResult::StartFailed;
    }
    return WifiConfigApAcquireResult::Acquired;
}

bool SharedWifiPolicy::acquireConfigAp(RuntimeOwner owner)
{
    return acquireConfigApDetailed(owner) == WifiConfigApAcquireResult::Acquired;
}

void SharedWifiPolicy::releaseConfigAp(RuntimeOwner owner)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_resources.releaseWifiAp(owner) == ReleaseResult::LastOwnerReleased) {
        _driver.stopConfigAp();
    }
}

bool SharedWifiPolicy::reconnectSta(RuntimeOwner owner)
{
    return reconfigureSta(owner, []() {}) == StaReconfigureResult::Applied;
}

StaReconfigureResult SharedWifiPolicy::reconfigureSta(
    RuntimeOwner owner, const std::function<void()>& configure)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto snapshot = _resources.snapshot();
    if (!_resources.ownsWifi(owner) || snapshot.wifi_ap_active ||
        snapshot.wifi_lease_count != 1) {
        return StaReconfigureResult::Busy;
    }

    _driver.stopSta();
    configure();
    return _driver.startSta() ? StaReconfigureResult::Applied
                              : StaReconfigureResult::StartFailed;
}

bool SharedWifiPolicy::staConnected() const
{
    return _driver.staConnected();
}

bool SharedWifiPolicy::cycleStaForValidation()
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto snapshot = _resources.snapshot();
    if (snapshot.wifi_lease_count == 0 || snapshot.wifi_ap_active) {
        return false;
    }
    _driver.stopSta();
    return _driver.startSta();
}
