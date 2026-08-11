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
    Disconnecting,
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
    uint32_t admitted_at_ms = 0;
    uint32_t last_pairing_activity_ms = 0;
};

struct AgentActiveConnection {
    uint16_t handle = kNoAgentConnection;
    uint32_t generation = 0;
};

struct AgentVoiceLink {
    uint16_t handle = kNoAgentConnection;
    uint32_t generation = 0;
    uint16_t mtu = kDefaultAgentMtu;
    bool subscribed = false;
    bool authorized = false;
};

class AgentConnectionRouter {
public:
    AgentConnectionAdmission connected(uint16_t handle, uint32_t now_ms = 0);
    AgentConnectionRole disconnected(uint16_t handle);

    bool authorize_pending(
        uint16_t handle,
        const AgentFingerprint& fingerprint);
    bool promote_pending(uint16_t handle);
    bool reject_pending(uint16_t handle);
    bool note_pending_activity(uint16_t handle, uint32_t now_ms);
    uint16_t expire_unauthorized_pending(
        uint32_t now_ms,
        uint32_t idle_timeout_ms,
        uint32_t max_lifetime_ms);

    AgentConnectionRole role(uint16_t handle) const;
    bool can_access(uint16_t handle, AgentGattSurface surface) const;
    bool update_mtu(uint16_t handle, uint16_t mtu);
    bool update_voice_subscription(uint16_t handle, bool enabled);
    bool update_pairing_subscription(uint16_t handle, bool enabled);

    uint16_t active_handle() const;
    AgentActiveConnection active_connection() const;
    AgentVoiceLink active_voice_link() const;
    uint16_t pending_handle() const;
    uint16_t disconnecting_handle() const;
    uint16_t mtu(uint16_t handle) const;
    AgentFingerprint fingerprint(uint16_t handle) const;
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
