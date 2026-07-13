/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <array>
#include <cstddef>

enum class RuntimeOwner { None, Assistant, Codex, Badge, System };
enum class AcquireResult { Granted, AlreadyOwned, Busy, Blocked };
enum class ReleaseResult { Released, LastOwnerReleased, NotOwned };

struct ResourceSnapshot {
    RuntimeOwner capture_owner  = RuntimeOwner::None;
    RuntimeOwner playback_owner = RuntimeOwner::None;
    std::size_t wifi_lease_count = 0;
    bool wifi_ap_active          = false;
    bool blocked                 = false;
};

class ResourceSupervisor {
public:
    AcquireResult acquireWifi(RuntimeOwner owner);
    ReleaseResult releaseWifi(RuntimeOwner owner);
    std::size_t wifiLeaseCount() const;

    AcquireResult acquireWifiAp(RuntimeOwner owner);
    ReleaseResult releaseWifiAp(RuntimeOwner owner);

    AcquireResult acquireCapture(RuntimeOwner owner);
    ReleaseResult releaseCapture(RuntimeOwner owner);

    AcquireResult acquirePlayback(RuntimeOwner owner);
    ReleaseResult releasePlayback(RuntimeOwner owner);

    void block();
    void unblock();
    bool isBlocked() const;
    ResourceSnapshot snapshot() const;

private:
    static constexpr std::size_t ownerCount = 5;

    static bool isValidOwner(RuntimeOwner owner);
    static std::size_t ownerIndex(RuntimeOwner owner);
    AcquireResult acquireExclusive(RuntimeOwner owner, RuntimeOwner& current_owner);
    static ReleaseResult releaseExclusive(RuntimeOwner owner, RuntimeOwner& current_owner);

    std::array<bool, ownerCount> _wifi_owners = {};
    RuntimeOwner _wifi_ap_owner                = RuntimeOwner::None;
    RuntimeOwner _capture_owner                = RuntimeOwner::None;
    RuntimeOwner _playback_owner               = RuntimeOwner::None;
    bool _blocked                              = false;
};
