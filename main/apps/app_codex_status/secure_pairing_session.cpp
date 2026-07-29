#include "secure_pairing_session.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace secure_pairing {
namespace {

constexpr std::array<uint8_t, 6> kAuthenticationMagic = {
    'S', 'W', 'A', 'U', 'T', 'H',
};
constexpr std::array<uint8_t, 6> kCredentialMagic = {
    'S', 'W', 'C', 'R', 'E', 'D',
};
constexpr std::array<uint8_t, 5> kEnvelopeMagic = {
    'S', 'W', 'E', 'N', 'V',
};
constexpr std::array<uint8_t, 5> kAckMagic = {
    'S', 'W', 'A', 'C', 'K',
};
constexpr size_t kEnvelopeAadSize = 11;
constexpr size_t kAckAadSize = 10;

void write_u32(uint32_t value, uint8_t* output)
{
    output[0] = static_cast<uint8_t>((value >> 24U) & 0xffU);
    output[1] = static_cast<uint8_t>((value >> 16U) & 0xffU);
    output[2] = static_cast<uint8_t>((value >> 8U) & 0xffU);
    output[3] = static_cast<uint8_t>(value & 0xffU);
}

bool nonzero(const uint8_t* value, size_t size)
{
    return value != nullptr &&
           std::any_of(value, value + size, [](uint8_t byte) {
               return byte != 0;
           });
}

bool fill_nonzero(
    RegistryEntropy entropy,
    void* context,
    uint8_t* output,
    size_t size)
{
    if (entropy == nullptr || output == nullptr) {
        return false;
    }
    for (int attempt = 0; attempt < 8; ++attempt) {
        std::fill_n(output, size, uint8_t{0});
        if (!entropy(context, output, size)) {
            return false;
        }
        if (nonzero(output, size)) {
            return true;
        }
    }
    return false;
}

const AgentRecord* find_public_key(
    const AgentRegistry& registry,
    const crypto::PublicKey& public_key)
{
    for (size_t index = 0; index < registry.count(); ++index) {
        if (registry.records()[index].public_key == public_key) {
            return &registry.records()[index];
        }
    }
    return nullptr;
}

bool encode_envelope_aad(
    const CredentialEnvelope& envelope,
    std::array<uint8_t, kEnvelopeAadSize>& output)
{
    if (envelope.transaction_id == 0 ||
        (envelope.purpose != CredentialPurpose::Pairing &&
         envelope.purpose != CredentialPurpose::Rekey)) {
        return false;
    }
    std::copy(kEnvelopeMagic.begin(), kEnvelopeMagic.end(), output.begin());
    output[5] = kPairingVersion;
    output[6] = static_cast<uint8_t>(envelope.purpose);
    write_u32(envelope.transaction_id, output.data() + 7);
    return true;
}

bool encode_ack_aad(
    const CredentialEnvelope& envelope,
    std::array<uint8_t, kAckAadSize>& output)
{
    if (envelope.transaction_id == 0) {
        return false;
    }
    std::copy(kAckMagic.begin(), kAckMagic.end(), output.begin());
    output[5] = kPairingVersion;
    write_u32(envelope.transaction_id, output.data() + 6);
    return true;
}

bool constant_equal(const uint8_t* lhs, const uint8_t* rhs, size_t size)
{
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }
    uint8_t difference = 0;
    for (size_t index = 0; index < size; ++index) {
        difference |= static_cast<uint8_t>(lhs[index] ^ rhs[index]);
    }
    return difference == 0;
}

}  // namespace

bool encode_authentication_challenge(
    const AuthenticationChallenge& value,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size)
{
    output_size = 0;
    if (output == nullptr ||
        output_capacity < kAuthenticationChallengeSize ||
        value.transaction_id == 0 || value.key_epoch == 0 ||
        value.connection_role != 1 ||
        !nonzero(value.watch_id.data(), value.watch_id.size()) ||
        !nonzero(
            value.connection_nonce.data(), value.connection_nonce.size())) {
        return false;
    }
    size_t offset = 0;
    std::copy(
        kAuthenticationMagic.begin(),
        kAuthenticationMagic.end(),
        output + offset);
    offset += kAuthenticationMagic.size();
    output[offset++] = kPairingVersion;
    output[offset++] = value.connection_role;
    write_u32(value.transaction_id, output + offset);
    offset += 4;
    std::copy(
        value.watch_id.begin(), value.watch_id.end(), output + offset);
    offset += value.watch_id.size();
    std::copy(
        value.connection_nonce.begin(),
        value.connection_nonce.end(),
        output + offset);
    offset += value.connection_nonce.size();
    write_u32(value.key_epoch, output + offset);
    offset += 4;
    output_size = offset;
    return output_size == kAuthenticationChallengeSize;
}

bool encode_credential_ack(
    const crypto::Key& ack_key,
    const CredentialEnvelope& envelope,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size)
{
    output_size = 0;
    std::array<uint8_t, 4> transaction = {};
    std::array<uint8_t, kAckAadSize> aad = {};
    write_u32(envelope.transaction_id, transaction.data());
    return encode_ack_aad(envelope, aad) &&
           crypto::wrap_credential(
               ack_key,
               envelope.ack_nonce,
               transaction.data(),
               transaction.size(),
               aad.data(),
               aad.size(),
               output,
               output_capacity,
               output_size) &&
           output_size == kCredentialAckSize;
}

SessionResult SecurePairingSession::request_pairing(
    const PairingRequest& request,
    const AgentRegistry& registry,
    RegistryEntropy entropy,
    void* entropy_context,
    uint64_t now_ms)
{
    if (!available(now_ms)) {
        return SessionResult::Cooldown;
    }
    if (_state != PairingState::Idle) {
        return SessionResult::Busy;
    }
    if (!registry.initialized() || request.connection_handle == 0xffff ||
        request.transaction_id == 0 ||
        !nonzero(request.agent_nonce.data(), request.agent_nonce.size()) ||
        !crypto::valid_public_key(request.agent_ephemeral_public) ||
        !crypto::valid_public_key(request.agent_identity_public)) {
        return SessionResult::InvalidRequest;
    }
    if (find_public_key(registry, request.agent_identity_public) != nullptr) {
        return SessionResult::DuplicateAgent;
    }
    if (registry.count() >= kMaxAgents) {
        return SessionResult::RegistryFull;
    }

    crypto::PublicKey watch_public = {};
    RandomValue watch_nonce = {};
    if (!generate_ephemeral(
            entropy,
            entropy_context,
            _watch_ephemeral_private,
            watch_public) ||
        !fill_nonzero(
            entropy,
            entropy_context,
            watch_nonce.data(),
            watch_nonce.size())) {
        clear_transaction(PairingState::Idle);
        return SessionResult::EntropyFailure;
    }

    PairingTranscript transcript = {};
    transcript.transaction_id = request.transaction_id;
    transcript.watch_id = registry.watch_id();
    transcript.watch_nonce = watch_nonce;
    transcript.agent_nonce = request.agent_nonce;
    transcript.watch_ephemeral_public = watch_public;
    transcript.agent_ephemeral_public = request.agent_ephemeral_public;
    transcript.agent_identity_public = request.agent_identity_public;
    transcript.agent_name = request.agent_name;
    transcript.agent_name_size = request.agent_name_size;
    std::array<uint8_t, kMaxTranscriptSize> encoded = {};
    size_t encoded_size = 0;
    crypto::SharedSecret shared = {};
    crypto::VerificationCode code = {};
    if (!encode_pairing_transcript(
            transcript, encoded.data(), encoded.size(), encoded_size) ||
        !crypto::derive_shared_secret(
            _watch_ephemeral_private,
            request.agent_ephemeral_public,
            shared) ||
        !crypto::derive_pairing_keys(
            shared, encoded.data(), encoded_size, _keys) ||
        !crypto::verification_code(
            _keys.confirm_key, encoded.data(), encoded_size, code)) {
        clear_transaction(PairingState::Idle);
        return SessionResult::CryptoFailure;
    }

    _pairing_request = request;
    _presentation.connection_handle = request.connection_handle;
    _presentation.transaction_id = request.transaction_id;
    _presentation.watch_id = registry.watch_id();
    _presentation.watch_nonce = watch_nonce;
    _presentation.watch_ephemeral_public = watch_public;
    _presentation.verification_code = code;
    _deadline_ms = now_ms + kPairingSessionTimeoutMs;
    _state = PairingState::AwaitingConfirmation;
    return SessionResult::Ok;
}

SessionResult SecurePairingSession::confirm_pairing(
    bool accepted,
    AgentRegistry& registry,
    AgentRegistryStore& store,
    RegistryEntropy entropy,
    void* entropy_context,
    uint64_t now_ms)
{
    if (expire(now_ms)) {
        return SessionResult::Expired;
    }
    if (_state != PairingState::AwaitingConfirmation) {
        return SessionResult::InvalidState;
    }
    if (!accepted) {
        return fail_attempt(SessionResult::Rejected, now_ms);
    }

    const RegistrySnapshot before = registry.snapshot();
    AgentEnrollment enrollment = {};
    enrollment.public_key = _pairing_request.agent_identity_public;
    enrollment.display_name_size = _pairing_request.agent_name_size;
    std::copy_n(
        _pairing_request.agent_name.begin(),
        enrollment.display_name_size,
        enrollment.display_name.begin());
    enrollment.display_name[enrollment.display_name_size] = '\0';
    const RegistryResult added = registry.add(enrollment);
    if (added != RegistryResult::Ok) {
        clear_transaction(PairingState::Idle);
        return added == RegistryResult::Full
                   ? SessionResult::RegistryFull
                   : SessionResult::InvalidRequest;
    }
    if (store.save(registry) != StoreResult::Ok) {
        (void)AgentRegistry::restore(before, registry);
        clear_transaction(PairingState::Idle);
        return SessionResult::StorageFailure;
    }
    const AgentRecord* record =
        find_public_key(registry, _pairing_request.agent_identity_public);
    if (record == nullptr ||
        !prepare_credential(
            CredentialPurpose::Pairing,
            registry,
            record->fingerprint,
            entropy,
            entropy_context,
            _presentation.watch_ephemeral_public)) {
        clear_transaction(PairingState::Idle);
        return SessionResult::CryptoFailure;
    }
    _presentation.credential_ready = true;
    _authenticated_fingerprint = record->fingerprint;
    _state = PairingState::AwaitingCredentialAck;
    return SessionResult::Ok;
}

SessionResult SecurePairingSession::acknowledge_credential(
    uint32_t transaction_id,
    const uint8_t* proof,
    size_t proof_size,
    uint64_t now_ms)
{
    if (expire(now_ms)) {
        return SessionResult::Expired;
    }
    if (_state != PairingState::AwaitingCredentialAck ||
        transaction_id != _presentation.credential.transaction_id) {
        return SessionResult::InvalidState;
    }
    std::array<uint8_t, kCredentialAckSize> expected = {};
    size_t expected_size = 0;
    if (!encode_credential_ack(
            _keys.ack_key,
            _presentation.credential,
            expected.data(),
            expected.size(),
            expected_size) ||
        proof_size != expected_size ||
        !constant_equal(proof, expected.data(), expected_size)) {
        return fail_attempt(SessionResult::AuthenticationFailed, now_ms);
    }
    const PairingState complete =
        _presentation.credential.purpose == CredentialPurpose::Pairing
            ? PairingState::Paired
            : PairingState::Authenticated;
    const AgentFingerprint authenticated = _authenticated_fingerprint;
    clear_transaction(complete);
    _authenticated_fingerprint = authenticated;
    _failure_count = 0;
    return SessionResult::Ok;
}

SessionResult SecurePairingSession::request_authentication(
    const AuthenticationRequest& request,
    const AgentRegistry& registry,
    RegistryEntropy entropy,
    void* entropy_context,
    uint64_t now_ms)
{
    if (!available(now_ms)) {
        return SessionResult::Cooldown;
    }
    if (_state != PairingState::Idle) {
        return SessionResult::Busy;
    }
    const AgentRecord* record = registry.find(request.fingerprint);
    if (request.connection_handle == 0xffff || request.transaction_id == 0 ||
        record == nullptr) {
        return record == nullptr
                   ? SessionResult::UnknownAgent
                   : SessionResult::InvalidRequest;
    }
    AuthenticationChallenge challenge = {};
    challenge.transaction_id = request.transaction_id;
    challenge.watch_id = registry.watch_id();
    challenge.key_epoch = registry.key_epoch();
    if (!fill_nonzero(
            entropy,
            entropy_context,
            challenge.connection_nonce.data(),
            challenge.connection_nonce.size())) {
        return SessionResult::EntropyFailure;
    }
    _authentication_request = request;
    _authentication_challenge = challenge;
    _presentation.connection_handle = request.connection_handle;
    _presentation.transaction_id = request.transaction_id;
    _presentation.watch_id = registry.watch_id();
    _authenticated_fingerprint = request.fingerprint;
    _deadline_ms = now_ms + kPairingSessionTimeoutMs;
    _state = PairingState::AwaitingAuthentication;
    return SessionResult::Ok;
}

SessionResult SecurePairingSession::authenticate(
    const uint8_t* signature,
    size_t signature_size,
    const AgentRegistry& registry,
    RegistryEntropy entropy,
    void* entropy_context,
    uint64_t now_ms)
{
    if (expire(now_ms)) {
        return SessionResult::Expired;
    }
    if (_state != PairingState::AwaitingAuthentication) {
        return SessionResult::InvalidState;
    }
    const AgentRecord* record =
        registry.find(_authentication_request.fingerprint);
    std::array<uint8_t, kAuthenticationChallengeSize> challenge = {};
    size_t challenge_size = 0;
    if (record == nullptr ||
        !encode_authentication_challenge(
            _authentication_challenge,
            challenge.data(),
            challenge.size(),
            challenge_size) ||
        !crypto::verify_challenge(
            record->public_key,
            challenge.data(),
            challenge_size,
            signature,
            signature_size) ||
        _authentication_request.agent_key_epoch > registry.key_epoch()) {
        return fail_attempt(SessionResult::AuthenticationFailed, now_ms);
    }
    if (_authentication_request.agent_key_epoch == registry.key_epoch()) {
        const AgentFingerprint authenticated = record->fingerprint;
        clear_transaction(PairingState::Authenticated);
        _authenticated_fingerprint = authenticated;
        _failure_count = 0;
        return SessionResult::Ok;
    }

    crypto::PublicKey watch_public = {};
    crypto::SharedSecret shared = {};
    if (!generate_ephemeral(
            entropy,
            entropy_context,
            _watch_ephemeral_private,
            watch_public) ||
        !crypto::derive_shared_secret(
            _watch_ephemeral_private, record->public_key, shared) ||
        !crypto::derive_pairing_keys(
            shared, challenge.data(), challenge_size, _keys) ||
        !prepare_credential(
            CredentialPurpose::Rekey,
            registry,
            record->fingerprint,
            entropy,
            entropy_context,
            watch_public)) {
        clear_transaction(PairingState::Idle);
        return SessionResult::CryptoFailure;
    }
    _presentation.watch_ephemeral_public = watch_public;
    _presentation.credential_ready = true;
    _state = PairingState::AwaitingCredentialAck;
    return SessionResult::Ok;
}

bool SecurePairingSession::expire(uint64_t now_ms)
{
    if ((_state == PairingState::AwaitingConfirmation ||
         _state == PairingState::AwaitingAuthentication ||
         _state == PairingState::AwaitingCredentialAck) &&
        now_ms > _deadline_ms) {
        clear_transaction(PairingState::Idle);
        return true;
    }
    if (_state == PairingState::Cooldown && now_ms >= _cooldown_until_ms) {
        clear_transaction(PairingState::Idle);
        _failure_count = 0;
    }
    return false;
}

void SecurePairingSession::reset()
{
    clear_transaction(
        _state == PairingState::Cooldown
            ? PairingState::Cooldown
            : PairingState::Idle);
}

PairingState SecurePairingSession::state() const
{
    return _state;
}

const PairingPresentation& SecurePairingSession::presentation() const
{
    return _presentation;
}

const AuthenticationChallenge&
SecurePairingSession::authentication_challenge() const
{
    return _authentication_challenge;
}

const AgentFingerprint&
SecurePairingSession::authenticated_fingerprint() const
{
    return _authenticated_fingerprint;
}

uint16_t SecurePairingSession::connection_handle() const
{
    if (_state == PairingState::AwaitingAuthentication) {
        return _authentication_request.connection_handle;
    }
    return _presentation.connection_handle;
}

uint8_t SecurePairingSession::failure_count() const
{
    return _failure_count;
}

uint64_t SecurePairingSession::deadline_ms() const
{
    return _deadline_ms;
}

bool SecurePairingSession::available(uint64_t now_ms)
{
    (void)expire(now_ms);
    if (_state == PairingState::Cooldown) {
        return false;
    }
    if (_state == PairingState::Paired ||
        _state == PairingState::Authenticated) {
        clear_transaction(PairingState::Idle);
    }
    return true;
}

SessionResult SecurePairingSession::fail_attempt(
    SessionResult result,
    uint64_t now_ms)
{
    if (_failure_count < 0xffU) {
        ++_failure_count;
    }
    if (_failure_count >= 3) {
        clear_transaction(PairingState::Cooldown);
        _cooldown_until_ms = now_ms + kPairingCooldownMs;
        return SessionResult::Cooldown;
    }
    clear_transaction(PairingState::Idle);
    return result;
}

void SecurePairingSession::clear_transaction(PairingState next_state)
{
    std::fill(
        _watch_ephemeral_private.begin(),
        _watch_ephemeral_private.end(),
        uint8_t{0});
    std::fill(
        _keys.confirm_key.begin(), _keys.confirm_key.end(), uint8_t{0});
    std::fill(_keys.wrap_key.begin(), _keys.wrap_key.end(), uint8_t{0});
    std::fill(_keys.ack_key.begin(), _keys.ack_key.end(), uint8_t{0});
    _pairing_request = {};
    _authentication_request = {};
    _authentication_challenge = {};
    _presentation = {};
    _authenticated_fingerprint = {};
    _deadline_ms = 0;
    _state = next_state;
}

bool SecurePairingSession::generate_ephemeral(
    RegistryEntropy entropy,
    void* entropy_context,
    crypto::PrivateScalar& private_scalar,
    crypto::PublicKey& public_key)
{
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (!fill_nonzero(
                entropy,
                entropy_context,
                private_scalar.data(),
                private_scalar.size())) {
            return false;
        }
        if (crypto::public_key_from_private(private_scalar, public_key)) {
            return true;
        }
    }
    std::fill(private_scalar.begin(), private_scalar.end(), uint8_t{0});
    public_key = {};
    return false;
}

bool SecurePairingSession::prepare_credential(
    CredentialPurpose purpose,
    const AgentRegistry& registry,
    const AgentFingerprint& fingerprint,
    RegistryEntropy entropy,
    void* entropy_context,
    const crypto::PublicKey& watch_ephemeral_public)
{
    CredentialEnvelope envelope = {};
    envelope.purpose = purpose;
    envelope.transaction_id =
        purpose == CredentialPurpose::Pairing
            ? _pairing_request.transaction_id
            : _authentication_request.transaction_id;
    envelope.key_epoch = registry.key_epoch();
    envelope.watch_ephemeral_public = watch_ephemeral_public;
    if (!fill_nonzero(
            entropy,
            entropy_context,
            envelope.nonce.data(),
            envelope.nonce.size()) ||
        !fill_nonzero(
            entropy,
            entropy_context,
            envelope.ack_nonce.data(),
            envelope.ack_nonce.size())) {
        return false;
    }

    std::array<uint8_t, kCredentialPlaintextSize> plaintext = {};
    size_t offset = 0;
    std::copy(
        kCredentialMagic.begin(), kCredentialMagic.end(), plaintext.begin());
    offset += kCredentialMagic.size();
    plaintext[offset++] = kPairingVersion;
    std::copy(
        registry.master_key().begin(),
        registry.master_key().end(),
        plaintext.begin() + static_cast<std::ptrdiff_t>(offset));
    offset += registry.master_key().size();
    write_u32(registry.key_epoch(), plaintext.data() + offset);
    offset += 4;
    std::copy(
        registry.watch_id().begin(),
        registry.watch_id().end(),
        plaintext.begin() + static_cast<std::ptrdiff_t>(offset));
    offset += registry.watch_id().size();
    std::copy(
        fingerprint.begin(),
        fingerprint.end(),
        plaintext.begin() + static_cast<std::ptrdiff_t>(offset));
    offset += fingerprint.size();
    if (offset != plaintext.size()) {
        return false;
    }
    std::array<uint8_t, kEnvelopeAadSize> aad = {};
    if (!encode_envelope_aad(envelope, aad) ||
        !crypto::wrap_credential(
            _keys.wrap_key,
            envelope.nonce,
            plaintext.data(),
            plaintext.size(),
            aad.data(),
            aad.size(),
            envelope.ciphertext.data(),
            envelope.ciphertext.size(),
            envelope.ciphertext_size) ||
        envelope.ciphertext_size != envelope.ciphertext.size()) {
        return false;
    }
    _presentation.credential = envelope;
    return true;
}

}  // namespace secure_pairing
