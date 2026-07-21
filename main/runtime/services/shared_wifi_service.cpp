/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "shared_wifi_service.h"

#include <utility>

WifiStaLease::WifiStaLease(SharedWifiService& service, RuntimeOwner owner)
    : _service(&service), _owner(owner)
{
}

WifiStaLease::WifiStaLease(WifiStaLease&& other) noexcept
    : _service(std::exchange(other._service, nullptr)),
      _owner(std::exchange(other._owner, RuntimeOwner::None))
{
}

WifiStaLease& WifiStaLease::operator=(WifiStaLease&& other) noexcept
{
    if (this != &other) {
        reset();
        _service = std::exchange(other._service, nullptr);
        _owner = std::exchange(other._owner, RuntimeOwner::None);
    }
    return *this;
}

WifiStaLease::~WifiStaLease()
{
    reset();
}

bool WifiStaLease::valid() const
{
    return _service != nullptr;
}

void WifiStaLease::reset()
{
    if (_service != nullptr) {
        auto* service = std::exchange(_service, nullptr);
        const RuntimeOwner owner = std::exchange(_owner, RuntimeOwner::None);
        service->releaseSta(owner);
    }
}

WifiApSession::WifiApSession(SharedWifiService& service, RuntimeOwner owner)
    : _service(&service), _owner(owner)
{
}

WifiApSession::WifiApSession(WifiApSession&& other) noexcept
    : _service(std::exchange(other._service, nullptr)),
      _owner(std::exchange(other._owner, RuntimeOwner::None))
{
}

WifiApSession& WifiApSession::operator=(WifiApSession&& other) noexcept
{
    if (this != &other) {
        reset();
        _service = std::exchange(other._service, nullptr);
        _owner = std::exchange(other._owner, RuntimeOwner::None);
    }
    return *this;
}

WifiApSession::~WifiApSession()
{
    reset();
}

bool WifiApSession::valid() const
{
    return _service != nullptr;
}

void WifiApSession::reset()
{
    if (_service != nullptr) {
        auto* service = std::exchange(_service, nullptr);
        const RuntimeOwner owner = std::exchange(_owner, RuntimeOwner::None);
        service->releaseAp(owner);
    }
}

WifiConfigApSession::WifiConfigApSession(SharedWifiService& service, RuntimeOwner owner)
    : _service(&service), _owner(owner)
{
}

WifiConfigApSession::WifiConfigApSession(WifiConfigApSession&& other) noexcept
    : _service(std::exchange(other._service, nullptr)),
      _owner(std::exchange(other._owner, RuntimeOwner::None))
{
}

WifiConfigApSession& WifiConfigApSession::operator=(WifiConfigApSession&& other) noexcept
{
    if (this != &other) {
        reset();
        _service = std::exchange(other._service, nullptr);
        _owner = std::exchange(other._owner, RuntimeOwner::None);
    }
    return *this;
}

WifiConfigApSession::~WifiConfigApSession()
{
    reset();
}

bool WifiConfigApSession::valid() const
{
    return _service != nullptr;
}

void WifiConfigApSession::reset()
{
    if (_service != nullptr) {
        auto* service = std::exchange(_service, nullptr);
        const RuntimeOwner owner = std::exchange(_owner, RuntimeOwner::None);
        service->releaseConfigAp(owner);
    }
}

SharedWifiService::SharedWifiService(SharedWifiPolicy& policy) : _policy(policy)
{
}

WifiStaLease SharedWifiService::acquireSta(RuntimeOwner owner)
{
    return _policy.acquireSta(owner) ? WifiStaLease(*this, owner) : WifiStaLease();
}

WifiApSession SharedWifiService::acquireAp(RuntimeOwner owner)
{
    return _policy.acquireAp(owner) ? WifiApSession(*this, owner) : WifiApSession();
}

WifiConfigApSession SharedWifiService::acquireConfigAp(RuntimeOwner owner)
{
    return _policy.acquireConfigAp(owner) ? WifiConfigApSession(*this, owner) : WifiConfigApSession();
}

bool SharedWifiService::reconnectSta(RuntimeOwner owner)
{
    return _policy.reconnectSta(owner);
}

bool SharedWifiService::staConnected() const
{
    return _policy.staConnected();
}

void SharedWifiService::releaseSta(RuntimeOwner owner)
{
    _policy.releaseSta(owner);
}

void SharedWifiService::releaseAp(RuntimeOwner owner)
{
    _policy.releaseAp(owner);
}

void SharedWifiService::releaseConfigAp(RuntimeOwner owner)
{
    _policy.releaseConfigAp(owner);
}
