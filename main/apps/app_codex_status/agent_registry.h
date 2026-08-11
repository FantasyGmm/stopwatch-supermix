#pragma once

#include "secure_pairing_crypto.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace secure_pairing {

constexpr size_t kMaxAgents = 8;
constexpr size_t kAgentNameBytes = 24;
constexpr size_t kAgentFingerprintBytes = 16;
constexpr size_t kWatchMasterKeyBytes = 16;

using IdentityPublicKey = crypto::PublicKey;
using AgentFingerprint = std::array<uint8_t, kAgentFingerprintBytes>;
using AgentName = std::array<char, kAgentNameBytes + 1>;
using WatchMasterKey = std::array<uint8_t, kWatchMasterKeyBytes>;
using RegistryEntropy = bool (*)(void*, uint8_t*, size_t);

struct AgentEnrollment {
    IdentityPublicKey public_key = {};
    AgentName display_name = {};
    size_t display_name_size = 0;
    int64_t last_connected_at = 0;
};

struct AgentRecord {
    IdentityPublicKey public_key = {};
    AgentFingerprint fingerprint = {};
    AgentName display_name = {};
    int64_t last_connected_at = 0;
};

struct RegistrySnapshot {
    std::array<uint8_t, 16> watch_id = {};
    crypto::PrivateScalar watch_identity_private = {};
    WatchMasterKey master_key = {};
    uint32_t key_epoch = 0;
    bool has_last_selected = false;
    AgentFingerprint last_selected = {};
    std::array<AgentRecord, kMaxAgents> records = {};
    size_t count = 0;
};

enum class RegistryResult : uint8_t {
    Ok,
    Full,
    Duplicate,
    InvalidName,
    InvalidIdentity,
    NotFound,
    EntropyFailure,
    EpochExhausted,
    InvalidRecord,
};

class AgentRegistry {
public:
    static RegistryResult initialize(
        RegistryEntropy entropy,
        void* entropy_context,
        AgentRegistry& output);
    static RegistryResult restore(
        const RegistrySnapshot& snapshot,
        AgentRegistry& output);

    RegistryResult add(const AgentEnrollment& enrollment);
    RegistryResult rename(
        const AgentFingerprint& fingerprint,
        const AgentName& display_name,
        size_t display_name_size);
    RegistryResult select(
        const AgentFingerprint& fingerprint,
        int64_t connected_at);
    RegistryResult remove_and_rotate(
        const AgentFingerprint& fingerprint,
        RegistryEntropy entropy,
        void* entropy_context);
    RegistryResult reset_all(
        RegistryEntropy entropy,
        void* entropy_context);

    const AgentRecord* find(const AgentFingerprint& fingerprint) const;
    AgentRecord* find(const AgentFingerprint& fingerprint);
    const std::array<AgentRecord, kMaxAgents>& records() const;
    size_t count() const;
    const std::array<uint8_t, 16>& watch_id() const;
    const crypto::PrivateScalar& watch_identity_private() const;
    bool watch_identity_public(crypto::PublicKey& output) const;
    const WatchMasterKey& master_key() const;
    uint32_t key_epoch() const;
    bool has_last_selected() const;
    const AgentFingerprint& last_selected() const;
    bool initialized() const;
    RegistrySnapshot snapshot() const;

private:
    RegistrySnapshot _data = {};
    bool _initialized = false;
};

bool operator==(const AgentRecord& lhs, const AgentRecord& rhs);
bool operator==(const AgentRegistry& lhs, const AgentRegistry& rhs);
bool operator!=(const AgentRegistry& lhs, const AgentRegistry& rhs);

}  // namespace secure_pairing
