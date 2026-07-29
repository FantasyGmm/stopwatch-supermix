#include "main/apps/app_codex_status/secure_pairing_crypto.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>

namespace {

template <size_t Size>
std::array<uint8_t, Size> from_hex(std::string_view hex)
{
    assert(hex.size() == Size * 2);
    std::array<uint8_t, Size> result = {};
    const auto nibble = [](char value) -> uint8_t {
        return value <= '9' ? static_cast<uint8_t>(value - '0')
                            : static_cast<uint8_t>(value - 'a' + 10);
    };
    for (size_t index = 0; index < Size; ++index) {
        result[index] = static_cast<uint8_t>(
            (nibble(hex[index * 2]) << 4U) | nibble(hex[index * 2 + 1]));
    }
    return result;
}

constexpr std::string_view kTranscriptHex =
    "5357504149520101020304"
    "0010d4c3b2a19080706050403020100f0e0d"
    "001000112233445566778899aabbccddeeff"
    "0010ffeeddccbbaa99887766554433221100"
    "0021020217e617f0b6443928278f96999e69"
    "a23a4f2c152bdf6d6cdf66e5b80282d4ed"
    "002103d65a93977caa3d1b081852ff57a79e"
    "465f1660577304baead505dd3a48589cf3"
    "00210351a7580833898ea1b183cbd7350a40"
    "99078c6ef1c1e18e970cd7683035f25e7d"
    "000a53747564696f204d6163";

}  // namespace

int main()
{
    using namespace secure_pairing::crypto;

    const PrivateScalar watch_private = from_hex<kPrivateScalarSize>(
        "11111111111111111111111111111111"
        "11111111111111111111111111111111");
    const PrivateScalar agent_private = from_hex<kPrivateScalarSize>(
        "22222222222222222222222222222222"
        "22222222222222222222222222222222");
    const PublicKey watch_public = from_hex<kPublicKeySize>(
        "020217e617f0b6443928278f96999e69"
        "a23a4f2c152bdf6d6cdf66e5b80282d4ed");
    const PublicKey agent_public = from_hex<kPublicKeySize>(
        "03d65a93977caa3d1b081852ff57a79e"
        "465f1660577304baead505dd3a48589cf3");
    const PublicKey identity_public = from_hex<kPublicKeySize>(
        "0351a7580833898ea1b183cbd7350a40"
        "99078c6ef1c1e18e970cd7683035f25e7d");
    const SharedSecret expected_shared = from_hex<kSharedSecretSize>(
        "ccfc261f58193c98ca4ad4a53bbac6f0"
        "ee29bc4d48438090446908622ca79af6");
    const auto transcript = from_hex<182>(kTranscriptHex);

    PublicKey derived_public = {};
    assert(public_key_from_private(watch_private, derived_public));
    assert(derived_public == watch_public);

    SharedSecret shared = {};
    assert(derive_shared_secret(watch_private, agent_public, shared));
    assert(shared == expected_shared);
    SharedSecret reverse_shared = {};
    assert(derive_shared_secret(agent_private, watch_public, reverse_shared));
    assert(reverse_shared == expected_shared);

    PairingKeys keys = {};
    assert(derive_pairing_keys(
        shared, transcript.data(), transcript.size(), keys));
    assert(keys.confirm_key ==
           from_hex<kKeySize>("6f6671511a2e699c9e45a1e9e0fffc51"));
    assert(keys.wrap_key ==
           from_hex<kKeySize>("96386cf8e7b4968c70cfe5d8eb81d127"));
    assert(keys.ack_key ==
           from_hex<kKeySize>("7340fea9148e4a1ae9885eb1a6137271"));

    VerificationCode code = {};
    assert(verification_code(
        keys.confirm_key, transcript.data(), transcript.size(), code));
    assert(std::string_view(code.data(), 6) == "421761");

    const Nonce wrap_nonce =
        from_hex<kNonceSize>("0102030405060708090a0b0c0d");
    const auto credential = from_hex<52>(
        "00112233445566778899aabbccddeeff"
        "00000007"
        "d4c3b2a19080706050403020100f0e0d"
        "2ea06a7bca900cd2410d78584bdc1add");
    const auto expected_wrapped = from_hex<60>(
        "e11a7485dada5a637d45b236530ccce70"
        "c06f0f699565a19be3ff0f523f9c56a"
        "4747b9a11f679f0c4a9153bfcb5f055b"
        "7ae3ded9e14981ca4292dc6b");
    std::array<uint8_t, 60> wrapped = {};
    size_t wrapped_size = 0;
    assert(wrap_credential(
        keys.wrap_key,
        wrap_nonce,
        credential.data(),
        credential.size(),
        transcript.data(),
        transcript.size(),
        wrapped.data(),
        wrapped.size(),
        wrapped_size));
    assert(wrapped_size == expected_wrapped.size());
    assert(wrapped == expected_wrapped);

    std::array<uint8_t, 52> plaintext = {};
    size_t plaintext_size = 0;
    assert(unwrap_credential(
        keys.wrap_key,
        wrap_nonce,
        wrapped.data(),
        wrapped.size(),
        transcript.data(),
        transcript.size(),
        plaintext.data(),
        plaintext.size(),
        plaintext_size));
    assert(plaintext_size == credential.size());
    assert(plaintext == credential);
    auto tampered_aad = transcript;
    tampered_aad.back() ^= 1;
    assert(!unwrap_credential(
        keys.wrap_key,
        wrap_nonce,
        wrapped.data(),
        wrapped.size(),
        tampered_aad.data(),
        tampered_aad.size(),
        plaintext.data(),
        plaintext.size(),
        plaintext_size));

    const auto signature = from_hex<72>(
        "3046022100f4375396f1fd8d0fc3b9c8dd5d445cf"
        "dfa2aab870312bee7f50f34fdc72c9ae5"
        "022100b523545ec23e87a683debe674ae4d2d19d"
        "a36167759afdfb0deaaf55713dbf16");
    assert(verify_challenge(
        identity_public,
        transcript.data(),
        transcript.size(),
        signature.data(),
        signature.size()));
    assert(!verify_challenge(
        identity_public,
        tampered_aad.data(),
        tampered_aad.size(),
        signature.data(),
        signature.size()));
}
