#pragma once

#include "agent_registry.h"

#include <array>
#include <cstdint>
#include <mutex>

namespace secure_pairing {

constexpr uint16_t kNoAgentConnection = 0xffff;
constexpr uint16_t kDefaultAgentMtu = 23;

enum class AgentConnectionRole : uint8_t {
    None,
    Pending,
    Active,
};

enum class AgentConnectionAdmission : uint8_t {
    Pending,
    Existing,
    RejectedFull,
};

enum class AgentGattSurface : uint8_t {
    Pairing,
    Production,
};

struct AgentConnection {
    uint16_t handle = kNoAgentConnection;
    AgentConnectionRole role = AgentConnectionRole::None;
    AgentFingerprint fingerprint = {};
    bool identity_authorized = false;
    uint16_t mtu = kDefaultAgentMtu;
    bool voice_subscribed = false;
    bool pairing_subscribed = false;
};

class AgentConnectionRouter {
public:
    AgentConnectionAdmission connected(uint16_t handle);
    AgentConnectionRole disconnected(uint16_t handle);

    bool authorize_pending(
        uint16_t handle,
        const AgentFingerprint& fingerprint);
    bool promote_pending(uint16_t handle);
    bool reject_pending(uint16_t handle);

    AgentConnectionRole role(uint16_t handle) const;
    bool can_access(uint16_t handle, AgentGattSurface surface) const;
    bool update_mtu(uint16_t handle, uint16_t mtu);
    bool update_voice_subscription(uint16_t handle, bool enabled);
    bool update_pairing_subscription(uint16_t handle, bool enabled);

    uint16_t active_handle() const;
    uint16_t pending_handle() const;
    uint16_t mtu(uint16_t handle) const;
    bool voice_subscribed(uint16_t handle) const;
    bool pairing_subscribed(uint16_t handle) const;
    uint16_t active_mtu() const;
    bool active_voice_subscribed() const;
    const AgentFingerprint active_fingerprint() const;
    uint32_t active_generation() const;
    bool has_connections() const;
    void clear();

private:
    AgentConnection* find_locked(uint16_t handle);
    const AgentConnection* find_locked(uint16_t handle) const;
    AgentConnection* empty_locked();
    const AgentConnection* role_locked(AgentConnectionRole role) const;

    mutable std::mutex _mutex;
    std::array<AgentConnection, 2> _connections = {};
    uint32_t _active_generation = 0;
};

}  // namespace secure_pairing
