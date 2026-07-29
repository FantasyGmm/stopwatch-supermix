#include "agent_connection_router.h"

#include <algorithm>

namespace secure_pairing {

AgentConnectionAdmission AgentConnectionRouter::connected(uint16_t handle)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (handle == kNoAgentConnection) {
        return AgentConnectionAdmission::RejectedFull;
    }
    if (find_locked(handle) != nullptr) {
        return AgentConnectionAdmission::Existing;
    }
    if (role_locked(AgentConnectionRole::Pending) != nullptr) {
        return AgentConnectionAdmission::RejectedFull;
    }
    AgentConnection* slot = empty_locked();
    if (slot == nullptr) {
        return AgentConnectionAdmission::RejectedFull;
    }
    *slot = {};
    slot->handle = handle;
    slot->role = AgentConnectionRole::Pending;
    return AgentConnectionAdmission::Pending;
}

AgentConnectionRole AgentConnectionRouter::disconnected(uint16_t handle)
{
    std::lock_guard<std::mutex> lock(_mutex);
    AgentConnection* connection = find_locked(handle);
    if (connection == nullptr) {
        return AgentConnectionRole::None;
    }
    const AgentConnectionRole previous = connection->role;
    *connection = {};
    if (previous == AgentConnectionRole::Active) {
        ++_active_generation;
    }
    return previous;
}

bool AgentConnectionRouter::authorize_pending(
    uint16_t handle,
    const AgentFingerprint& fingerprint)
{
    std::lock_guard<std::mutex> lock(_mutex);
    AgentConnection* connection = find_locked(handle);
    if (connection == nullptr ||
        connection->role != AgentConnectionRole::Pending ||
        !std::any_of(
            fingerprint.begin(), fingerprint.end(), [](uint8_t byte) {
                return byte != 0;
            })) {
        return false;
    }
    connection->fingerprint = fingerprint;
    connection->identity_authorized = true;
    return true;
}

bool AgentConnectionRouter::promote_pending(uint16_t handle)
{
    std::lock_guard<std::mutex> lock(_mutex);
    AgentConnection* connection = find_locked(handle);
    if (connection == nullptr ||
        connection->role != AgentConnectionRole::Pending ||
        !connection->identity_authorized ||
        role_locked(AgentConnectionRole::Active) != nullptr) {
        return false;
    }
    connection->role = AgentConnectionRole::Active;
    connection->pairing_subscribed = false;
    ++_active_generation;
    return true;
}

bool AgentConnectionRouter::reject_pending(uint16_t handle)
{
    std::lock_guard<std::mutex> lock(_mutex);
    AgentConnection* connection = find_locked(handle);
    if (connection == nullptr ||
        connection->role != AgentConnectionRole::Pending) {
        return false;
    }
    *connection = {};
    return true;
}

AgentConnectionRole AgentConnectionRouter::role(uint16_t handle) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AgentConnection* connection = find_locked(handle);
    return connection == nullptr
               ? AgentConnectionRole::None
               : connection->role;
}

bool AgentConnectionRouter::can_access(
    uint16_t handle,
    AgentGattSurface surface) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AgentConnection* connection = find_locked(handle);
    if (connection == nullptr) {
        return false;
    }
    return (surface == AgentGattSurface::Production &&
            connection->role == AgentConnectionRole::Active &&
            connection->identity_authorized) ||
           (surface == AgentGattSurface::Pairing &&
            connection->role == AgentConnectionRole::Pending);
}

bool AgentConnectionRouter::update_mtu(uint16_t handle, uint16_t mtu_value)
{
    std::lock_guard<std::mutex> lock(_mutex);
    AgentConnection* connection = find_locked(handle);
    if (connection == nullptr || mtu_value < kDefaultAgentMtu) {
        return false;
    }
    connection->mtu = mtu_value;
    return true;
}

bool AgentConnectionRouter::update_voice_subscription(
    uint16_t handle,
    bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);
    AgentConnection* connection = find_locked(handle);
    if (connection == nullptr) {
        return false;
    }
    connection->voice_subscribed = enabled;
    return true;
}

bool AgentConnectionRouter::update_pairing_subscription(
    uint16_t handle,
    bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);
    AgentConnection* connection = find_locked(handle);
    if (connection == nullptr) {
        return false;
    }
    connection->pairing_subscribed = enabled;
    return true;
}

uint16_t AgentConnectionRouter::active_handle() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AgentConnection* connection =
        role_locked(AgentConnectionRole::Active);
    return connection == nullptr ? kNoAgentConnection : connection->handle;
}

uint16_t AgentConnectionRouter::pending_handle() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AgentConnection* connection =
        role_locked(AgentConnectionRole::Pending);
    return connection == nullptr ? kNoAgentConnection : connection->handle;
}

uint16_t AgentConnectionRouter::mtu(uint16_t handle) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AgentConnection* connection = find_locked(handle);
    return connection == nullptr ? kDefaultAgentMtu : connection->mtu;
}

AgentFingerprint AgentConnectionRouter::fingerprint(uint16_t handle) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AgentConnection* connection = find_locked(handle);
    return connection == nullptr
               ? AgentFingerprint{}
               : connection->fingerprint;
}

bool AgentConnectionRouter::voice_subscribed(uint16_t handle) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AgentConnection* connection = find_locked(handle);
    return connection != nullptr && connection->voice_subscribed;
}

bool AgentConnectionRouter::pairing_subscribed(uint16_t handle) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AgentConnection* connection = find_locked(handle);
    return connection != nullptr && connection->pairing_subscribed;
}

uint16_t AgentConnectionRouter::active_mtu() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AgentConnection* connection =
        role_locked(AgentConnectionRole::Active);
    return connection == nullptr ? kDefaultAgentMtu : connection->mtu;
}

bool AgentConnectionRouter::active_voice_subscribed() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AgentConnection* connection =
        role_locked(AgentConnectionRole::Active);
    return connection != nullptr && connection->voice_subscribed;
}

const AgentFingerprint AgentConnectionRouter::active_fingerprint() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AgentConnection* connection =
        role_locked(AgentConnectionRole::Active);
    return connection == nullptr ? AgentFingerprint{} : connection->fingerprint;
}

uint32_t AgentConnectionRouter::active_generation() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _active_generation;
}

bool AgentConnectionRouter::has_connections() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return std::any_of(
        _connections.begin(),
        _connections.end(),
        [](const AgentConnection& connection) {
            return connection.role != AgentConnectionRole::None;
        });
}

void AgentConnectionRouter::clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (role_locked(AgentConnectionRole::Active) != nullptr) {
        ++_active_generation;
    }
    _connections = {};
}

AgentConnection* AgentConnectionRouter::find_locked(uint16_t handle)
{
    const auto found = std::find_if(
        _connections.begin(),
        _connections.end(),
        [handle](const AgentConnection& connection) {
            return connection.handle == handle &&
                   connection.role != AgentConnectionRole::None;
        });
    return found == _connections.end() ? nullptr : &*found;
}

const AgentConnection* AgentConnectionRouter::find_locked(
    uint16_t handle) const
{
    return const_cast<AgentConnectionRouter*>(this)->find_locked(handle);
}

AgentConnection* AgentConnectionRouter::empty_locked()
{
    const auto found = std::find_if(
        _connections.begin(),
        _connections.end(),
        [](const AgentConnection& connection) {
            return connection.role == AgentConnectionRole::None;
        });
    return found == _connections.end() ? nullptr : &*found;
}

const AgentConnection* AgentConnectionRouter::role_locked(
    AgentConnectionRole role_value) const
{
    const auto found = std::find_if(
        _connections.begin(),
        _connections.end(),
        [role_value](const AgentConnection& connection) {
            return connection.role == role_value;
        });
    return found == _connections.end() ? nullptr : &*found;
}

}  // namespace secure_pairing
