#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace secure_pairing::crypto {

constexpr size_t kPrivateScalarSize = 32;
constexpr size_t kPublicKeySize = 33;
constexpr size_t kSharedSecretSize = 32;
constexpr size_t kKeySize = 16;
constexpr size_t kNonceSize = 13;
constexpr size_t kTagSize = 8;
constexpr size_t kMaxSignatureSize = 73;

using PrivateScalar = std::array<uint8_t, kPrivateScalarSize>;
using PublicKey = std::array<uint8_t, kPublicKeySize>;
using SharedSecret = std::array<uint8_t, kSharedSecretSize>;
using Key = std::array<uint8_t, kKeySize>;
using Nonce = std::array<uint8_t, kNonceSize>;
using VerificationCode = std::array<char, 7>;

struct PairingKeys {
    Key confirm_key = {};
    Key wrap_key = {};
    Key ack_key = {};
};

bool valid_public_key(const PublicKey& public_key);
bool public_key_from_private(
    const PrivateScalar& private_scalar,
    PublicKey& output);
bool derive_shared_secret(
    const PrivateScalar& private_scalar,
    const PublicKey& peer_public_key,
    SharedSecret& output);
bool derive_pairing_keys(
    const SharedSecret& shared_secret,
    const uint8_t* transcript,
    size_t transcript_size,
    PairingKeys& output);
bool verification_code(
    const Key& confirm_key,
    const uint8_t* transcript,
    size_t transcript_size,
    VerificationCode& output);
bool sign_challenge(
    const PrivateScalar& private_scalar,
    const uint8_t* challenge,
    size_t challenge_size,
    uint8_t* signature,
    size_t signature_capacity,
    size_t& signature_size);
bool verify_challenge(
    const PublicKey& public_key,
    const uint8_t* challenge,
    size_t challenge_size,
    const uint8_t* signature,
    size_t signature_size);
bool wrap_credential(
    const Key& key,
    const Nonce& nonce,
    const uint8_t* plaintext,
    size_t plaintext_size,
    const uint8_t* aad,
    size_t aad_size,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size);
bool unwrap_credential(
    const Key& key,
    const Nonce& nonce,
    const uint8_t* ciphertext,
    size_t ciphertext_size,
    const uint8_t* aad,
    size_t aad_size,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size);

}  // namespace secure_pairing::crypto
