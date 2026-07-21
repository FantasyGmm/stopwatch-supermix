/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "shared_wifi_policy.h"

class SharedWifiService;

class WifiStaLease {
public:
    WifiStaLease() = default;
    WifiStaLease(const WifiStaLease&) = delete;
    WifiStaLease& operator=(const WifiStaLease&) = delete;
    WifiStaLease(WifiStaLease&& other) noexcept;
    WifiStaLease& operator=(WifiStaLease&& other) noexcept;
    ~WifiStaLease();

    bool valid() const;
    void reset();

private:
    friend class SharedWifiService;
    WifiStaLease(SharedWifiService& service, RuntimeOwner owner);

    SharedWifiService* _service = nullptr;
    RuntimeOwner _owner = RuntimeOwner::None;
};

class WifiApSession {
public:
    WifiApSession() = default;
    WifiApSession(const WifiApSession&) = delete;
    WifiApSession& operator=(const WifiApSession&) = delete;
    WifiApSession(WifiApSession&& other) noexcept;
    WifiApSession& operator=(WifiApSession&& other) noexcept;
    ~WifiApSession();

    bool valid() const;
    void reset();

private:
    friend class SharedWifiService;
    WifiApSession(SharedWifiService& service, RuntimeOwner owner);

    SharedWifiService* _service = nullptr;
    RuntimeOwner _owner = RuntimeOwner::None;
};

class WifiConfigApSession {
public:
    WifiConfigApSession() = default;
    WifiConfigApSession(const WifiConfigApSession&) = delete;
    WifiConfigApSession& operator=(const WifiConfigApSession&) = delete;
    WifiConfigApSession(WifiConfigApSession&& other) noexcept;
    WifiConfigApSession& operator=(WifiConfigApSession&& other) noexcept;
    ~WifiConfigApSession();

    bool valid() const;
    void reset();

private:
    friend class SharedWifiService;
    WifiConfigApSession(SharedWifiService& service, RuntimeOwner owner);

    SharedWifiService* _service = nullptr;
    RuntimeOwner _owner = RuntimeOwner::None;
};

struct WifiConfigApAcquire {
    WifiConfigApAcquireResult result = WifiConfigApAcquireResult::Busy;
    WifiConfigApSession session;
};

struct WifiConfiguredStaAcquire {
    WifiConfiguredStaAcquireResult result = WifiConfiguredStaAcquireResult::Busy;
    WifiStaLease lease;
};

class SharedWifiService {
public:
    explicit SharedWifiService(SharedWifiPolicy& policy);

    WifiStaLease acquireSta(RuntimeOwner owner);
    WifiConfiguredStaAcquire acquireConfiguredStaDetailed(RuntimeOwner owner);
    WifiApSession acquireAp(RuntimeOwner owner);
    WifiConfigApAcquire acquireConfigApDetailed(RuntimeOwner owner);
    WifiConfigApSession acquireConfigAp(RuntimeOwner owner);
    bool reconnectSta(RuntimeOwner owner);
    StaReconfigureResult reconfigureSta(RuntimeOwner owner,
                                        const std::function<void()>& configure);
    bool staConnected() const;

private:
    friend class WifiStaLease;
    friend class WifiApSession;
    void releaseSta(RuntimeOwner owner);
    friend class WifiConfigApSession;
    void releaseAp(RuntimeOwner owner);

    void releaseConfigAp(RuntimeOwner owner);
    SharedWifiPolicy& _policy;
};

SharedWifiService& GetSharedWifiService();
