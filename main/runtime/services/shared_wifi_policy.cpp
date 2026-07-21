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

bool SharedWifiPolicy::acquireConfigAp(RuntimeOwner owner)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AcquireResult result = _resources.acquireWifiAp(owner);
    if (result != AcquireResult::Granted) {
        return false;
    }

    if (!_driver.startConfigAp()) {
        _resources.releaseWifiAp(owner);
        return false;
    }
    return true;
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
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_resources.ownsWifi(owner) || _resources.snapshot().wifi_ap_active) {
        return false;
    }

    _driver.stopSta();
    return _driver.startSta();
}

bool SharedWifiPolicy::staConnected() const
{
    return _driver.staConnected();
}
