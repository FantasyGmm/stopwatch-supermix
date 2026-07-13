/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "runtime/resource_supervisor.h"

#include <mutex>

class SharedWifiDriver {
public:
    virtual ~SharedWifiDriver() = default;

    virtual bool startSta() = 0;
    virtual void stopSta() = 0;
    virtual bool startAp() = 0;
    virtual void stopAp() = 0;
    virtual bool staRequiredBySystem() const = 0;
};

class SharedWifiPolicy {
public:
    SharedWifiPolicy(ResourceSupervisor& resources, SharedWifiDriver& driver);

    bool acquireSta(RuntimeOwner owner);
    void releaseSta(RuntimeOwner owner);
    bool acquireAp(RuntimeOwner owner);
    void releaseAp(RuntimeOwner owner);

private:
    ResourceSupervisor& _resources;
    SharedWifiDriver& _driver;
    std::mutex _mutex;
};
