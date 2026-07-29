#include "secure_pairing_crypto.h"

#include <mbedtls/ccm.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#if defined(ESP_PLATFORM)
#include <esp_random.h>
#endif

namespace secure_pairing::crypto {
namespace {

constexpr std::array<uint8_t, 22> kConfirmLabel = {
    's', 't', 'o', 'p', 'w', 'a', 't', 'c', 'h', '/', 'p', 'a',
    'i', 'r', '/', 'c', 'o', 'n', 'f', 'i', 'r', 'm',
};
constexpr std::array<uint8_t, 19> kWrapLabel = {
    's', 't', 'o', 'p', 'w', 'a', 't', 'c', 'h', '/',
    'p', 'a', 'i', 'r', '/', 'w', 'r', 'a', 'p',
};
constexpr std::array<uint8_t, 18> kAckLabel = {
    's', 't', 'o', 'p', 'w', 'a', 't', 'c', 'h', '/',
    'p', 'a', 'i', 'r', '/', 'a', 'c', 'k',
};

int crypto_random(void*, unsigned char* output, size_t size)
{
    if (output == nullptr) {
        return -1;
    }
#if defined(ESP_PLATFORM)
    esp_fill_random(output, size);
#elif defined(STOPWATCH_HOST_TEST)
    static uint64_t state = 0x98f37a4b2c1d5e60ULL;
    for (size_t index = 0; index < size; ++index) {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        output[index] = static_cast<uint8_t>(state & 0xffU);
    }
#else
    return -1;
#endif
    return 0;
}

bool nonzero(const Nonce& nonce)
{
    return std::any_of(nonce.begin(), nonce.end(), [](uint8_t byte) {
        return byte != 0;
    });
}

bool load_group(mbedtls_ecp_group& group)
{
    return mbedtls_ecp_group_load(
               &group, MBEDTLS_ECP_DP_SECP256R1) == 0;
}

bool load_private(
    const PrivateScalar& encoded,
    const mbedtls_ecp_group& group,
    mbedtls_mpi& private_scalar)
{
    return mbedtls_mpi_read_binary(
               &private_scalar, encoded.data(), encoded.size()) == 0 &&
           mbedtls_ecp_check_privkey(&group, &private_scalar) == 0;
}

bool load_public(
    const PublicKey& encoded,
    const mbedtls_ecp_group& group,
    mbedtls_ecp_point& public_key)
{
    return encoded[0] >= 2 && encoded[0] <= 3 &&
           mbedtls_ecp_point_read_binary(
               &group,
               &public_key,
               encoded.data(),
               encoded.size()) == 0 &&
           mbedtls_ecp_check_pubkey(&group, &public_key) == 0;
}

bool hash_sha256(
    const uint8_t* input,
    size_t input_size,
    std::array<uint8_t, 32>& output)
{
    return input != nullptr && input_size != 0 &&
           mbedtls_sha256(
               input, input_size, output.data(), 0) == 0;
}

template <size_t LabelSize>
bool hkdf_key(
    const SharedSecret& shared_secret,
    const std::array<uint8_t, 32>& salt,
    const std::array<uint8_t, LabelSize>& label,
    Key& output)
{
    const mbedtls_md_info_t* sha256 =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return sha256 != nullptr &&
           mbedtls_hkdf(
               sha256,
               salt.data(),
               salt.size(),
               shared_secret.data(),
               shared_secret.size(),
               label.data(),
               label.size(),
               output.data(),
               output.size()) == 0;
}

bool set_ccm_key(mbedtls_ccm_context& context, const Key& key)
{
    return mbedtls_ccm_setkey(
               &context,
               MBEDTLS_CIPHER_ID_AES,
               key.data(),
               key.size() * 8U) == 0;
}

}  // namespace

bool valid_public_key(const PublicKey& public_key)
{
    mbedtls_ecp_group group = {};
    mbedtls_ecp_point point = {};
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    const bool valid = load_group(group) && load_public(public_key, group, point);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return valid;
}

bool public_key_from_private(
    const PrivateScalar& private_scalar,
    PublicKey& output)
{
    output = {};
    mbedtls_ecp_group group = {};
    mbedtls_ecp_point public_key = {};
    mbedtls_mpi private_key = {};
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&public_key);
    mbedtls_mpi_init(&private_key);
    size_t output_size = 0;
    const bool ok =
        load_group(group) && load_private(private_scalar, group, private_key) &&
        mbedtls_ecp_mul(
            &group,
            &public_key,
            &private_key,
            &group.G,
            crypto_random,
            nullptr) == 0 &&
        mbedtls_ecp_point_write_binary(
            &group,
            &public_key,
            MBEDTLS_ECP_PF_COMPRESSED,
            &output_size,
            output.data(),
            output.size()) == 0 &&
        output_size == output.size();
    mbedtls_mpi_free(&private_key);
    mbedtls_ecp_point_free(&public_key);
    mbedtls_ecp_group_free(&group);
    if (!ok) {
        output = {};
    }
    return ok;
}

bool derive_shared_secret(
    const PrivateScalar& private_scalar,
    const PublicKey& peer_public_key,
    SharedSecret& output)
{
    output = {};
    mbedtls_ecp_group group = {};
    mbedtls_ecp_point public_key = {};
    mbedtls_mpi private_key = {};
    mbedtls_mpi shared = {};
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&public_key);
    mbedtls_mpi_init(&private_key);
    mbedtls_mpi_init(&shared);
    const bool ok =
        load_group(group) && load_private(private_scalar, group, private_key) &&
        load_public(peer_public_key, group, public_key) &&
        mbedtls_ecdh_compute_shared(
            &group,
            &shared,
            &public_key,
            &private_key,
            crypto_random,
            nullptr) == 0 &&
        mbedtls_mpi_write_binary(
            &shared, output.data(), output.size()) == 0;
    mbedtls_mpi_free(&shared);
    mbedtls_mpi_free(&private_key);
    mbedtls_ecp_point_free(&public_key);
    mbedtls_ecp_group_free(&group);
    if (!ok) {
        output = {};
    }
    return ok;
}

bool derive_pairing_keys(
    const SharedSecret& shared_secret,
    const uint8_t* transcript,
    size_t transcript_size,
    PairingKeys& output)
{
    output = {};
    std::array<uint8_t, 32> salt = {};
    if (!hash_sha256(transcript, transcript_size, salt) ||
        !hkdf_key(shared_secret, salt, kConfirmLabel, output.confirm_key) ||
        !hkdf_key(shared_secret, salt, kWrapLabel, output.wrap_key) ||
        !hkdf_key(shared_secret, salt, kAckLabel, output.ack_key)) {
        output = {};
        return false;
    }
    return true;
}

bool verification_code(
    const Key& confirm_key,
    const uint8_t* transcript,
    size_t transcript_size,
    VerificationCode& output)
{
    output = {};
    if (transcript == nullptr || transcript_size == 0) {
        return false;
    }
    const mbedtls_md_info_t* sha256 =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    std::array<uint8_t, 32> digest = {};
    if (sha256 == nullptr ||
        mbedtls_md_hmac(
            sha256,
            confirm_key.data(),
            confirm_key.size(),
            transcript,
            transcript_size,
            digest.data()) != 0) {
        return false;
    }
    const uint32_t prefix =
        (static_cast<uint32_t>(digest[0]) << 24U) |
        (static_cast<uint32_t>(digest[1]) << 16U) |
        (static_cast<uint32_t>(digest[2]) << 8U) | digest[3];
    const int written = std::snprintf(
        output.data(), output.size(), "%06u", prefix % 1000000U);
    return written == 6;
}

bool sign_challenge(
    const PrivateScalar& private_scalar,
    const uint8_t* challenge,
    size_t challenge_size,
    uint8_t* signature,
    size_t signature_capacity,
    size_t& signature_size)
{
    signature_size = 0;
    if (challenge == nullptr || challenge_size == 0 || signature == nullptr ||
        signature_capacity < kMaxSignatureSize) {
        return false;
    }
    std::array<uint8_t, 32> digest = {};
    if (!hash_sha256(challenge, challenge_size, digest)) {
        return false;
    }
    mbedtls_ecdsa_context context = {};
    mbedtls_ecdsa_init(&context);
    const bool ok =
        load_group(context.MBEDTLS_PRIVATE(grp)) &&
        load_private(
            private_scalar,
            context.MBEDTLS_PRIVATE(grp),
            context.MBEDTLS_PRIVATE(d)) &&
        mbedtls_ecp_mul(
            &context.MBEDTLS_PRIVATE(grp),
            &context.MBEDTLS_PRIVATE(Q),
            &context.MBEDTLS_PRIVATE(d),
            &context.MBEDTLS_PRIVATE(grp).G,
            crypto_random,
            nullptr) == 0 &&
        mbedtls_ecdsa_write_signature(
            &context,
            MBEDTLS_MD_SHA256,
            digest.data(),
            digest.size(),
            signature,
            signature_capacity,
            &signature_size,
            crypto_random,
            nullptr) == 0;
    mbedtls_ecdsa_free(&context);
    if (!ok) {
        signature_size = 0;
    }
    return ok;
}

bool verify_challenge(
    const PublicKey& public_key,
    const uint8_t* challenge,
    size_t challenge_size,
    const uint8_t* signature,
    size_t signature_size)
{
    if (challenge == nullptr || challenge_size == 0 || signature == nullptr ||
        signature_size == 0 || signature_size > kMaxSignatureSize) {
        return false;
    }
    std::array<uint8_t, 32> digest = {};
    if (!hash_sha256(challenge, challenge_size, digest)) {
        return false;
    }
    mbedtls_ecdsa_context context = {};
    mbedtls_ecdsa_init(&context);
    const bool ok =
        load_group(context.MBEDTLS_PRIVATE(grp)) &&
        load_public(
            public_key,
            context.MBEDTLS_PRIVATE(grp),
            context.MBEDTLS_PRIVATE(Q)) &&
        mbedtls_ecdsa_read_signature(
            &context,
            digest.data(),
            digest.size(),
            signature,
            signature_size) == 0;
    mbedtls_ecdsa_free(&context);
    return ok;
}

bool wrap_credential(
    const Key& key,
    const Nonce& nonce,
    const uint8_t* plaintext,
    size_t plaintext_size,
    const uint8_t* aad,
    size_t aad_size,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size)
{
    output_size = 0;
    if (!nonzero(nonce) || plaintext == nullptr || plaintext_size == 0 ||
        aad == nullptr || aad_size == 0 || output == nullptr ||
        output_capacity < plaintext_size + kTagSize) {
        return false;
    }
    mbedtls_ccm_context context = {};
    mbedtls_ccm_init(&context);
    const bool ok =
        set_ccm_key(context, key) &&
        mbedtls_ccm_encrypt_and_tag(
            &context,
            plaintext_size,
            nonce.data(),
            nonce.size(),
            aad,
            aad_size,
            plaintext,
            output,
            output + plaintext_size,
            kTagSize) == 0;
    mbedtls_ccm_free(&context);
    if (!ok) {
        return false;
    }
    output_size = plaintext_size + kTagSize;
    return true;
}

bool unwrap_credential(
    const Key& key,
    const Nonce& nonce,
    const uint8_t* ciphertext,
    size_t ciphertext_size,
    const uint8_t* aad,
    size_t aad_size,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size)
{
    output_size = 0;
    if (!nonzero(nonce) || ciphertext == nullptr ||
        ciphertext_size < kTagSize || aad == nullptr || aad_size == 0 ||
        output == nullptr || output_capacity < ciphertext_size - kTagSize) {
        return false;
    }
    const size_t plaintext_size = ciphertext_size - kTagSize;
    mbedtls_ccm_context context = {};
    mbedtls_ccm_init(&context);
    const bool ok =
        set_ccm_key(context, key) &&
        mbedtls_ccm_auth_decrypt(
            &context,
            plaintext_size,
            nonce.data(),
            nonce.size(),
            aad,
            aad_size,
            ciphertext,
            output,
            ciphertext + plaintext_size,
            kTagSize) == 0;
    mbedtls_ccm_free(&context);
    if (!ok) {
        std::fill_n(output, plaintext_size, uint8_t{0});
        return false;
    }
    output_size = plaintext_size;
    return true;
}

}  // namespace secure_pairing::crypto
