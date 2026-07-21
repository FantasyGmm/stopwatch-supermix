/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "resource_supervisor.h"
#include <algorithm>

namespace {
ResourceSupervisor resource_supervisor;
}

ResourceSupervisor& GetResourceSupervisor()
{
    return resource_supervisor;
}

bool ResourceSupervisor::isValidOwner(RuntimeOwner owner)
{
    return owner != RuntimeOwner::None && ownerIndex(owner) < ownerCount;
}

std::size_t ResourceSupervisor::ownerIndex(RuntimeOwner owner)
{
    return static_cast<std::size_t>(owner);
}

AcquireResult ResourceSupervisor::acquireWifi(RuntimeOwner owner)
{
    if (!isValidOwner(owner)) {
        return AcquireResult::Busy;
    }

    bool& owned = _wifi_owners[ownerIndex(owner)];
    if (owned) {
        return AcquireResult::AlreadyOwned;
    }
    if (_blocked) {
        return AcquireResult::Blocked;
    }
    if (_wifi_ap_owner != RuntimeOwner::None) {
        return AcquireResult::Busy;
    }

    owned = true;
    return AcquireResult::Granted;
}

ReleaseResult ResourceSupervisor::releaseWifi(RuntimeOwner owner)
{
    if (!isValidOwner(owner)) {
        return ReleaseResult::NotOwned;
    }

    bool& owned = _wifi_owners[ownerIndex(owner)];
    if (!owned) {
        return ReleaseResult::NotOwned;
    }

    owned = false;
    return wifiLeaseCount() == 0 ? ReleaseResult::LastOwnerReleased : ReleaseResult::Released;
}

std::size_t ResourceSupervisor::wifiLeaseCount() const
{
    return static_cast<std::size_t>(std::count(_wifi_owners.begin(), _wifi_owners.end(), true));
}

bool ResourceSupervisor::ownsWifi(RuntimeOwner owner) const
{
    return isValidOwner(owner) && _wifi_owners[ownerIndex(owner)];
}

AcquireResult ResourceSupervisor::acquireWifiAp(RuntimeOwner owner)
{
    if (!isValidOwner(owner)) {
        return AcquireResult::Busy;
    }
    if (_wifi_ap_owner == owner) {
        return AcquireResult::AlreadyOwned;
    }
    if (_blocked) {
        return AcquireResult::Blocked;
    }
    if (_wifi_ap_owner != RuntimeOwner::None || wifiLeaseCount() != 0) {
        return AcquireResult::Busy;
    }

    _wifi_ap_owner = owner;
    return AcquireResult::Granted;
}

ReleaseResult ResourceSupervisor::releaseWifiAp(RuntimeOwner owner)
{
    if (!isValidOwner(owner) || _wifi_ap_owner != owner) {
        return ReleaseResult::NotOwned;
    }

    _wifi_ap_owner = RuntimeOwner::None;
    return ReleaseResult::LastOwnerReleased;
}

AcquireResult ResourceSupervisor::acquireExclusive(RuntimeOwner owner, RuntimeOwner& current_owner)
{
    if (!isValidOwner(owner)) {
        return AcquireResult::Busy;
    }
    if (current_owner == owner) {
        return AcquireResult::AlreadyOwned;
    }
    if (_blocked) {
        return AcquireResult::Blocked;
    }
    if (current_owner != RuntimeOwner::None) {
        return AcquireResult::Busy;
    }

    current_owner = owner;
    return AcquireResult::Granted;
}

ReleaseResult ResourceSupervisor::releaseExclusive(RuntimeOwner owner, RuntimeOwner& current_owner)
{
    if (!isValidOwner(owner) || current_owner != owner) {
        return ReleaseResult::NotOwned;
    }

    current_owner = RuntimeOwner::None;
    return ReleaseResult::Released;
}

AcquireResult ResourceSupervisor::acquireCapture(RuntimeOwner owner)
{
    return acquireExclusive(owner, _capture_owner);
}

ReleaseResult ResourceSupervisor::releaseCapture(RuntimeOwner owner)
{
    return releaseExclusive(owner, _capture_owner);
}

AcquireResult ResourceSupervisor::acquirePlayback(RuntimeOwner owner)
{
    return acquireExclusive(owner, _playback_owner);
}

ReleaseResult ResourceSupervisor::releasePlayback(RuntimeOwner owner)
{
    return releaseExclusive(owner, _playback_owner);
}

void ResourceSupervisor::block()
{
    _blocked = true;
}

void ResourceSupervisor::unblock()
{
    _blocked = false;
}

bool ResourceSupervisor::isBlocked() const
{
    return _blocked;
}

ResourceSnapshot ResourceSupervisor::snapshot() const
{
    return {
        .capture_owner   = _capture_owner,
        .playback_owner  = _playback_owner,
        .wifi_lease_count = wifiLeaseCount(),
        .wifi_ap_active   = _wifi_ap_owner != RuntimeOwner::None,
        .blocked          = _blocked,
    };
}
