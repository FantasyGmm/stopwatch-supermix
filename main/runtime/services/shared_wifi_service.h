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

class SharedWifiService {
public:
    explicit SharedWifiService(SharedWifiPolicy& policy);

    WifiStaLease acquireSta(RuntimeOwner owner);
    WifiApSession acquireAp(RuntimeOwner owner);
    bool staConnected() const;

private:
    friend class WifiStaLease;
    friend class WifiApSession;
    void releaseSta(RuntimeOwner owner);
    void releaseAp(RuntimeOwner owner);

    SharedWifiPolicy& _policy;
};

SharedWifiService& GetSharedWifiService();
