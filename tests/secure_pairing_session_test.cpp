#include "main/apps/app_codex_status/secure_pairing_session.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct Entropy {
    uint8_t next = 1;
    bool fail = false;
};

bool fill_entropy(void* context, uint8_t* output, size_t size)
{
    auto& entropy = *static_cast<Entropy*>(context);
    if (entropy.fail) {
        return false;
    }
    for (size_t index = 0; index < size; ++index) {
        output[index] = entropy.next++;
    }
    return true;
}

class MemoryBackend : public secure_pairing::RegistryStorageBackend {
public:
    secure_pairing::StorageIoResult read(
        const char* key,
        uint8_t* output,
        size_t& size) override
    {
        const auto found = committed.find(key);
        if (found == committed.end()) {
            return secure_pairing::StorageIoResult::NotFound;
        }
        if (size < found->second.size()) {
            size = found->second.size();
            return secure_pairing::StorageIoResult::BufferTooSmall;
        }
        std::copy(found->second.begin(), found->second.end(), output);
        size = found->second.size();
        return secure_pairing::StorageIoResult::Ok;
    }

    secure_pairing::StorageIoResult write(
        const char* key,
        const uint8_t* input,
        size_t size) override
    {
        staged[key] = std::vector<uint8_t>(input, input + size);
        return secure_pairing::StorageIoResult::Ok;
    }

    secure_pairing::StorageIoResult commit() override
    {
        if (fail_commit) {
            fail_commit = false;
            staged.clear();
            return secure_pairing::StorageIoResult::Error;
        }
        for (const auto& [key, value] : staged) {
            committed[key] = value;
        }
        staged.clear();
        return secure_pairing::StorageIoResult::Ok;
    }

    std::map<std::string, std::vector<uint8_t>> staged;
    std::map<std::string, std::vector<uint8_t>> committed;
    bool fail_commit = false;
};

secure_pairing::PairingRequest request(
    const secure_pairing::crypto::PrivateScalar& ephemeral_private,
    const secure_pairing::crypto::PrivateScalar& identity_private,
    uint32_t transaction_id = 42)
{
    secure_pairing::PairingRequest value = {};
    value.connection_handle = 7;
    value.transaction_id = transaction_id;
    for (size_t index = 0; index < value.agent_nonce.size(); ++index) {
        value.agent_nonce[index] = static_cast<uint8_t>(0xa0U + index);
    }
    assert(secure_pairing::crypto::public_key_from_private(
        ephemeral_private, value.agent_ephemeral_public));
    assert(secure_pairing::crypto::public_key_from_private(
        identity_private, value.agent_identity_public));
    constexpr char kName[] = "Studio Mac";
    value.agent_name_size = sizeof(kName) - 1;
    std::copy_n(kName, value.agent_name_size, value.agent_name.begin());
    return value;
}

std::vector<uint8_t> pairing_ack(
    const secure_pairing::PairingRequest& request_value,
    const secure_pairing::PairingPresentation& presentation,
    const secure_pairing::crypto::PrivateScalar& agent_ephemeral_private,
    const secure_pairing::CredentialEnvelope& envelope)
{
    secure_pairing::PairingTranscript transcript = {};
    transcript.transaction_id = request_value.transaction_id;
    transcript.watch_id = presentation.watch_id;
    transcript.watch_nonce = presentation.watch_nonce;
    transcript.agent_nonce = request_value.agent_nonce;
    transcript.watch_ephemeral_public = presentation.watch_ephemeral_public;
    transcript.agent_ephemeral_public = request_value.agent_ephemeral_public;
    transcript.agent_identity_public = request_value.agent_identity_public;
    transcript.agent_name = request_value.agent_name;
    transcript.agent_name_size = request_value.agent_name_size;
    std::array<uint8_t, secure_pairing::kMaxTranscriptSize> encoded = {};
    size_t encoded_size = 0;
    assert(secure_pairing::encode_pairing_transcript(
        transcript, encoded.data(), encoded.size(), encoded_size));

    secure_pairing::crypto::SharedSecret shared = {};
    secure_pairing::crypto::PairingKeys keys = {};
    assert(secure_pairing::crypto::derive_shared_secret(
        agent_ephemeral_private,
        presentation.watch_ephemeral_public,
        shared));
    assert(secure_pairing::crypto::derive_pairing_keys(
        shared, encoded.data(), encoded_size, keys));
    std::vector<uint8_t> proof(secure_pairing::kCredentialAckSize);
    size_t proof_size = 0;
    assert(secure_pairing::encode_credential_ack(
        keys.ack_key,
        envelope,
        proof.data(),
        proof.size(),
        proof_size));
    proof.resize(proof_size);
    return proof;
}

std::vector<uint8_t> authentication_ack(
    const secure_pairing::AuthenticationChallenge& challenge,
    const secure_pairing::crypto::PrivateScalar& identity_private,
    const secure_pairing::CredentialEnvelope& envelope)
{
    std::array<uint8_t, secure_pairing::kAuthenticationChallengeSize> encoded = {};
    size_t encoded_size = 0;
    assert(secure_pairing::encode_authentication_challenge(
        challenge, encoded.data(), encoded.size(), encoded_size));
    secure_pairing::crypto::SharedSecret shared = {};
    secure_pairing::crypto::PairingKeys keys = {};
    assert(secure_pairing::crypto::derive_shared_secret(
        identity_private, envelope.watch_ephemeral_public, shared));
    assert(secure_pairing::crypto::derive_pairing_keys(
        shared, encoded.data(), encoded_size, keys));
    std::vector<uint8_t> proof(secure_pairing::kCredentialAckSize);
    size_t proof_size = 0;
    assert(secure_pairing::encode_credential_ack(
        keys.ack_key,
        envelope,
        proof.data(),
        proof.size(),
        proof_size));
    proof.resize(proof_size);
    return proof;
}

secure_pairing::AgentFingerprint fingerprint_for(
    const secure_pairing::AgentRegistry& registry,
    const secure_pairing::crypto::PublicKey& public_key)
{
    for (size_t index = 0; index < registry.count(); ++index) {
        if (registry.records()[index].public_key == public_key) {
            return registry.records()[index].fingerprint;
        }
    }
    assert(false);
    return {};
}

}  // namespace

int main()
{
    using namespace secure_pairing;

    Entropy entropy = {};
    AgentRegistry registry = {};
    assert(
        AgentRegistry::initialize(fill_entropy, &entropy, registry) ==
        RegistryResult::Ok);
    MemoryBackend backend;
    AgentRegistryStore store(backend);
    assert(store.save(registry) == StoreResult::Ok);

    crypto::PrivateScalar agent_ephemeral_private = {};
    crypto::PrivateScalar agent_identity_private = {};
    agent_ephemeral_private.back() = 11;
    agent_identity_private.back() = 13;
    const PairingRequest pairing_request =
        request(agent_ephemeral_private, agent_identity_private);

    SecurePairingSession session;
    assert(
        session.request_pairing(
            pairing_request, registry, fill_entropy, &entropy, 1000) ==
        SessionResult::Ok);
    assert(session.state() == PairingState::AwaitingConfirmation);
    assert(std::strlen(session.presentation().verification_code.data()) == 6);
    assert(!session.presentation().credential_ready);

    assert(
        session.confirm_pairing(true, registry, store, fill_entropy, &entropy, 1100) ==
        SessionResult::Ok);
    assert(session.state() == PairingState::AwaitingCredentialAck);
    assert(session.presentation().credential_ready);
    assert(registry.count() == 1);
    AgentRegistry persisted = {};
    assert(store.load(persisted) == StoreResult::Ok);
    assert(persisted == registry);

    const auto ack = pairing_ack(
        pairing_request,
        session.presentation(),
        agent_ephemeral_private,
        session.presentation().credential);
    assert(
        session.acknowledge_credential(
            pairing_request.transaction_id,
            ack.data(),
            ack.size(),
            1200) == SessionResult::Ok);
    assert(session.state() == PairingState::Paired);

    const AgentFingerprint fingerprint =
        fingerprint_for(registry, pairing_request.agent_identity_public);
    AuthenticationRequest authentication = {};
    authentication.connection_handle = 9;
    authentication.transaction_id = 43;
    authentication.fingerprint = fingerprint;
    authentication.agent_key_epoch = registry.key_epoch();
    constexpr char kRenamed[] = "Renamed Mac";
    authentication.display_name_size = sizeof(kRenamed) - 1;
    std::copy_n(
        kRenamed,
        authentication.display_name_size,
        authentication.display_name.begin());
    assert(
        session.request_authentication(
            authentication, registry, fill_entropy, &entropy, 2000) ==
        SessionResult::Ok);
    assert(session.state() == PairingState::AwaitingAuthentication);
    std::array<uint8_t, kAuthenticationChallengeSize> challenge = {};
    size_t challenge_size = 0;
    assert(encode_authentication_challenge(
        session.authentication_challenge(),
        challenge.data(),
        challenge.size(),
        challenge_size));
    std::array<uint8_t, crypto::kMaxSignatureSize> signature = {};
    size_t signature_size = 0;
    assert(crypto::sign_challenge(
        agent_identity_private,
        challenge.data(),
        challenge_size,
        signature.data(),
        signature.size(),
        signature_size));
    assert(
        session.authenticate(
            signature.data(),
            signature_size,
            registry,
            store,
            fill_entropy,
            &entropy,
            2100) == SessionResult::Ok);
    assert(session.state() == PairingState::Authenticated);
    assert(!session.presentation().credential_ready);
    assert(std::string(registry.find(fingerprint)->display_name.data()) ==
           "Renamed Mac");
    assert(store.load(persisted) == StoreResult::Ok);
    assert(std::string(persisted.find(fingerprint)->display_name.data()) ==
           "Renamed Mac");

    AgentRegistry rotated = registry;
    const auto second = request(
        crypto::PrivateScalar{{
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 17}},
        crypto::PrivateScalar{{
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 19}},
        44);
    AgentEnrollment second_enrollment = {};
    second_enrollment.public_key = second.agent_identity_public;
    second_enrollment.display_name_size = second.agent_name_size;
    std::copy_n(
        second.agent_name.begin(),
        second.agent_name_size,
        second_enrollment.display_name.begin());
    assert(rotated.add(second_enrollment) == RegistryResult::Ok);
    assert(
        rotated.remove_and_rotate(
            fingerprint_for(rotated, second.agent_identity_public),
            fill_entropy,
            &entropy) == RegistryResult::Ok);
    assert(store.save(rotated) == StoreResult::Ok);

    authentication.transaction_id = 45;
    authentication.agent_key_epoch = registry.key_epoch();
    assert(
        session.request_authentication(
            authentication, rotated, fill_entropy, &entropy, 3000) ==
        SessionResult::Ok);
    challenge_size = 0;
    assert(encode_authentication_challenge(
        session.authentication_challenge(),
        challenge.data(),
        challenge.size(),
        challenge_size));
    signature_size = 0;
    assert(crypto::sign_challenge(
        agent_identity_private,
        challenge.data(),
        challenge_size,
        signature.data(),
        signature.size(),
        signature_size));
    assert(
        session.authenticate(
            signature.data(),
            signature_size,
            rotated,
            store,
            fill_entropy,
            &entropy,
            3100) == SessionResult::Ok);
    assert(session.state() == PairingState::AwaitingCredentialAck);
    assert(session.presentation().credential_ready);
    assert(
        session.presentation().credential.key_epoch == rotated.key_epoch());
    const auto rekey_ack = authentication_ack(
        session.authentication_challenge(),
        agent_identity_private,
        session.presentation().credential);
    assert(
        session.acknowledge_credential(
            authentication.transaction_id,
            rekey_ack.data(),
            rekey_ack.size(),
            3200) == SessionResult::Ok);
    assert(session.state() == PairingState::Authenticated);

    AuthenticationRequest removed = authentication;
    removed.transaction_id = 46;
    removed.fingerprint = {};
    assert(
        session.request_authentication(
            removed, rotated, fill_entropy, &entropy, 4000) ==
        SessionResult::UnknownAgent);

    session.reset();
    assert(
        session.request_pairing(
            pairing_request, rotated, fill_entropy, &entropy, 5000) ==
        SessionResult::DuplicateAgent);

    crypto::PrivateScalar unknown_identity = {};
    unknown_identity.back() = 23;
    crypto::PrivateScalar unknown_ephemeral = {};
    unknown_ephemeral.back() = 29;
    PairingRequest unknown =
        request(unknown_ephemeral, unknown_identity, 50);
    assert(
        session.request_pairing(
            unknown, rotated, fill_entropy, &entropy, 5000) ==
        SessionResult::Ok);
    assert(session.confirm_pairing(false, rotated, store, fill_entropy, &entropy, 5100) ==
           SessionResult::Rejected);
    unknown.transaction_id = 51;
    assert(
        session.request_pairing(
            unknown, rotated, fill_entropy, &entropy, 5200) ==
        SessionResult::Ok);
    assert(session.confirm_pairing(false, rotated, store, fill_entropy, &entropy, 5300) ==
           SessionResult::Rejected);
    unknown.transaction_id = 52;
    assert(
        session.request_pairing(
            unknown, rotated, fill_entropy, &entropy, 5400) ==
        SessionResult::Ok);
    assert(session.confirm_pairing(false, rotated, store, fill_entropy, &entropy, 5500) ==
           SessionResult::Cooldown);
    assert(session.state() == PairingState::Cooldown);
    unknown.transaction_id = 53;
    assert(
        session.request_pairing(
            unknown, rotated, fill_entropy, &entropy, 6000) ==
        SessionResult::Cooldown);
    session.reset();
    assert(
        session.request_pairing(
            unknown, rotated, fill_entropy, &entropy, 6001) ==
        SessionResult::Cooldown);
    assert(
        session.request_pairing(
            unknown, rotated, fill_entropy, &entropy, 65501) ==
        SessionResult::Ok);
    const uint64_t fixed_deadline = session.deadline_ms();
    const uint8_t invalid_proof = 0;
    assert(
        session.acknowledge_credential(
            unknown.transaction_id, &invalid_proof, 1, 70000) ==
        SessionResult::InvalidState);
    assert(session.deadline_ms() == fixed_deadline);
    assert(session.expire(125502));
    assert(session.state() == PairingState::Idle);

    SecurePairingSession commit_failure;
    AgentRegistry before_failure = rotated;
    unknown.transaction_id = 60;
    assert(
        commit_failure.request_pairing(
            unknown, rotated, fill_entropy, &entropy, 130000) ==
        SessionResult::Ok);
    backend.fail_commit = true;
    assert(
        commit_failure.confirm_pairing(
            true, rotated, store, fill_entropy, &entropy, 130100) ==
        SessionResult::StorageFailure);
    assert(rotated == before_failure);
    assert(!commit_failure.presentation().credential_ready);
}
