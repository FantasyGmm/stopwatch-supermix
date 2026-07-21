/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "runtime/resource_supervisor.h"

#include <functional>
#include <mutex>

class SharedWifiDriver {
public:
    virtual ~SharedWifiDriver() = default;

    virtual bool startSta() = 0;
    virtual void stopSta() = 0;
    virtual bool startAp() = 0;
    virtual void stopAp() = 0;
    virtual bool startConfigAp() = 0;
    virtual void stopConfigAp() = 0;
    virtual bool staRequiredBySystem() const = 0;
    virtual bool staConnected() const = 0;
    virtual bool hasSavedCredentials() const = 0;
};

enum class WifiConfigApAcquireResult { Acquired, Busy, StartFailed };
enum class WifiConfiguredStaAcquireResult { Acquired, NoCredentials, Busy, StartFailed };
enum class StaReconfigureResult { Applied, Busy, StartFailed };

class SharedWifiPolicy {
public:
    SharedWifiPolicy(ResourceSupervisor& resources, SharedWifiDriver& driver);

    bool acquireSta(RuntimeOwner owner);
    WifiConfiguredStaAcquireResult acquireConfiguredSta(RuntimeOwner owner);
    void releaseSta(RuntimeOwner owner);
    bool acquireAp(RuntimeOwner owner);
    void releaseAp(RuntimeOwner owner);
    WifiConfigApAcquireResult acquireConfigApDetailed(RuntimeOwner owner);
    bool acquireConfigAp(RuntimeOwner owner);
    void releaseConfigAp(RuntimeOwner owner);
    bool reconnectSta(RuntimeOwner owner);
    StaReconfigureResult reconfigureSta(RuntimeOwner owner,
                                        const std::function<void()>& configure);
    bool staConnected() const;

private:
    ResourceSupervisor& _resources;
    SharedWifiDriver& _driver;
    std::mutex _mutex;
};
