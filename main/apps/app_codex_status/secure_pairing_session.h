#pragma once

#include "agent_registry.h"
#include "agent_registry_store.h"
#include "secure_pairing_protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace secure_pairing {

constexpr uint64_t kPairingSessionTimeoutMs = 60000;
constexpr uint64_t kPairingCooldownMs = 60000;
constexpr size_t kAuthenticationChallengeFixedSize = 82;
constexpr size_t kAuthenticationChallengeSize =
    kAuthenticationChallengeFixedSize + kAgentNameBytes;
constexpr size_t kCredentialPlaintextSize = 92;
constexpr size_t kCredentialCiphertextSize =
    kCredentialPlaintextSize + crypto::kTagSize;
constexpr size_t kCredentialAckSize = 4 + crypto::kTagSize;

enum class CredentialPurpose : uint8_t {
    Pairing = 1,
    Rekey = 2,
};

enum class PairingState : uint8_t {
    Idle,
    AwaitingConfirmation,
    AwaitingAuthentication,
    AwaitingCredentialAck,
    Paired,
    Authenticated,
    Cooldown,
};

enum class SessionResult : uint8_t {
    Ok,
    Busy,
    InvalidRequest,
    InvalidState,
    UnknownAgent,
    DuplicateAgent,
    RegistryFull,
    Rejected,
    AuthenticationFailed,
    StorageFailure,
    CryptoFailure,
    EntropyFailure,
    Expired,
    Cooldown,
};

struct PairingRequest {
    uint16_t connection_handle = 0xffff;
    uint32_t transaction_id = 0;
    RandomValue agent_nonce = {};
    crypto::PublicKey agent_ephemeral_public = {};
    crypto::PublicKey agent_identity_public = {};
    std::array<char, kMaxAgentNameBytes> agent_name = {};
    size_t agent_name_size = 0;
    std::array<uint8_t, crypto::kMaxSignatureSize> identity_signature = {};
    size_t identity_signature_size = 0;
};

struct AuthenticationRequest {
    uint16_t connection_handle = 0xffff;
    uint32_t transaction_id = 0;
    AgentFingerprint fingerprint = {};
    uint32_t agent_key_epoch = 0;
    AgentName display_name = {};
    size_t display_name_size = 0;
};

struct AuthenticationChallenge {
    uint32_t transaction_id = 0;
    WatchId watch_id = {};
    RandomValue connection_nonce = {};
    uint32_t key_epoch = 0;
    uint8_t connection_role = 1;
    crypto::PublicKey watch_ephemeral_public = {};
    AgentName display_name = {};
    size_t display_name_size = 0;
};

struct CredentialEnvelope {
    CredentialPurpose purpose = CredentialPurpose::Pairing;
    uint32_t transaction_id = 0;
    uint32_t key_epoch = 0;
    crypto::PublicKey watch_ephemeral_public = {};
    crypto::Nonce nonce = {};
    std::array<uint8_t, kCredentialCiphertextSize> ciphertext = {};
    size_t ciphertext_size = 0;
    crypto::Nonce ack_nonce = {};
};

struct PairingPresentation {
    uint16_t connection_handle = 0xffff;
    uint32_t transaction_id = 0;
    WatchId watch_id = {};
    RandomValue watch_nonce = {};
    crypto::PublicKey watch_ephemeral_public = {};
    crypto::PublicKey watch_identity_public = {};
    crypto::VerificationCode verification_code = {};
    bool credential_ready = false;
    CredentialEnvelope credential = {};
};

bool encode_authentication_challenge(
    const AuthenticationChallenge& value,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size);

bool encode_credential_ack(
    const crypto::Key& ack_key,
    const CredentialEnvelope& envelope,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size);

class SecurePairingSession {
public:
    SessionResult request_pairing(
        const PairingRequest& request,
        const AgentRegistry& registry,
        RegistryEntropy entropy,
        void* entropy_context,
        uint64_t now_ms);
    SessionResult confirm_pairing(
        bool accepted,
        AgentRegistry& registry,
        AgentRegistryStore& store,
        RegistryEntropy entropy,
        void* entropy_context,
        uint64_t now_ms);
    SessionResult acknowledge_credential(
        uint32_t transaction_id,
        const uint8_t* proof,
        size_t proof_size,
        uint64_t now_ms);

    SessionResult request_authentication(
        const AuthenticationRequest& request,
        const AgentRegistry& registry,
        RegistryEntropy entropy,
        void* entropy_context,
        uint64_t now_ms);
    SessionResult authenticate(
        const uint8_t* signature,
        size_t signature_size,
        AgentRegistry& registry,
        AgentRegistryStore& store,
        RegistryEntropy entropy,
        void* entropy_context,
        uint64_t now_ms);

    bool expire(uint64_t now_ms);
    void reset();

    PairingState state() const;
    const PairingPresentation& presentation() const;
    const AuthenticationChallenge& authentication_challenge() const;
    const uint8_t* watch_challenge_signature() const;
    size_t watch_challenge_signature_size() const;
    const AgentFingerprint& authenticated_fingerprint() const;
    uint16_t connection_handle() const;
    uint8_t failure_count() const;
    uint64_t deadline_ms() const;

private:
    bool available(uint64_t now_ms);
    SessionResult fail_attempt(SessionResult result, uint64_t now_ms);
    void clear_transaction(PairingState next_state);
    bool generate_ephemeral(
        RegistryEntropy entropy,
        void* entropy_context,
        crypto::PrivateScalar& private_scalar,
        crypto::PublicKey& public_key);
    bool prepare_credential(
        CredentialPurpose purpose,
        const AgentRegistry& registry,
        const AgentFingerprint& fingerprint,
        RegistryEntropy entropy,
        void* entropy_context,
        const crypto::PublicKey& watch_ephemeral_public);

    PairingState _state = PairingState::Idle;
    uint32_t _deadline_ms = 0;
    uint32_t _cooldown_until_ms = 0;
    uint8_t _failure_count = 0;
    PairingRequest _pairing_request = {};
    bool _pairing_existing_identity = false;
    AuthenticationRequest _authentication_request = {};
    AuthenticationChallenge _authentication_challenge = {};
    std::array<uint8_t, crypto::kMaxSignatureSize>
        _watch_challenge_signature = {};
    size_t _watch_challenge_signature_size = 0;
    PairingPresentation _presentation = {};
    AgentFingerprint _authenticated_fingerprint = {};
    crypto::PrivateScalar _watch_ephemeral_private = {};
    crypto::PairingKeys _keys = {};
};

}  // namespace secure_pairing
