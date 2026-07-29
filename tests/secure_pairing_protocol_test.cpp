#include "main/apps/app_codex_status/secure_pairing_protocol.h"

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

secure_pairing::PairingTranscript fixture()
{
    using namespace secure_pairing;
    PairingTranscript value = {};
    value.transaction_id = 0x01020304U;
    value.watch_id =
        from_hex<kWatchIdSize>("d4c3b2a19080706050403020100f0e0d");
    value.watch_nonce =
        from_hex<kRandomSize>("00112233445566778899aabbccddeeff");
    value.agent_nonce =
        from_hex<kRandomSize>("ffeeddccbbaa99887766554433221100");
    value.watch_ephemeral_public = from_hex<kPublicKeySize>(
        "020217e617f0b6443928278f96999e69"
        "a23a4f2c152bdf6d6cdf66e5b80282d4ed");
    value.watch_identity_public = value.watch_ephemeral_public;
    value.agent_ephemeral_public = from_hex<kPublicKeySize>(
        "03d65a93977caa3d1b081852ff57a79e"
        "465f1660577304baead505dd3a48589cf3");
    value.agent_identity_public = from_hex<kPublicKeySize>(
        "0351a7580833898ea1b183cbd7350a40"
        "99078c6ef1c1e18e970cd7683035f25e7d");
    constexpr std::string_view name = "Studio Mac";
    value.agent_name_size = name.size();
    std::copy(name.begin(), name.end(), value.agent_name.begin());
    return value;
}

secure_pairing::PairingIdentityClaim identity_claim_fixture()
{
    using namespace secure_pairing;
    const PairingTranscript transcript = fixture();
    PairingIdentityClaim value = {};
    value.transaction_id = transcript.transaction_id;
    value.agent_nonce = transcript.agent_nonce;
    value.agent_ephemeral_public = transcript.agent_ephemeral_public;
    value.agent_identity_public = transcript.agent_identity_public;
    value.agent_name = transcript.agent_name;
    value.agent_name_size = transcript.agent_name_size;
    return value;
}

}  // namespace

int main()
{
    using namespace secure_pairing;
    const auto expected = from_hex<217>(
        "5357504149520101020304"
        "0010d4c3b2a19080706050403020100f0e0d"
        "001000112233445566778899aabbccddeeff"
        "0010ffeeddccbbaa99887766554433221100"
        "0021020217e617f0b6443928278f96999e69"
        "a23a4f2c152bdf6d6cdf66e5b80282d4ed"
        "0021020217e617f0b6443928278f96999e69"
        "a23a4f2c152bdf6d6cdf66e5b80282d4ed"
        "002103d65a93977caa3d1b081852ff57a79e"
        "465f1660577304baead505dd3a48589cf3"
        "00210351a7580833898ea1b183cbd7350a40"
        "99078c6ef1c1e18e970cd7683035f25e7d"
        "000a53747564696f204d6163");

    std::array<uint8_t, kMaxTranscriptSize> encoded = {};
    size_t encoded_size = 0;
    assert(encode_pairing_transcript(
        fixture(), encoded.data(), encoded.size(), encoded_size));
    assert(encoded_size == expected.size());
    assert(std::equal(expected.begin(), expected.end(), encoded.begin()));

    const auto expected_identity_claim = from_hex<111>(
        "535750434c4d0101020304"
        "0010ffeeddccbbaa99887766554433221100"
        "002103d65a93977caa3d1b081852ff57a79e"
        "465f1660577304baead505dd3a48589cf3"
        "00210351a7580833898ea1b183cbd7350a40"
        "99078c6ef1c1e18e970cd7683035f25e7d"
        "000a53747564696f204d6163");
    std::array<uint8_t, kMaxIdentityClaimSize> encoded_identity_claim = {};
    size_t encoded_identity_claim_size = 0;
    assert(encode_pairing_identity_claim(
        identity_claim_fixture(),
        encoded_identity_claim.data(),
        encoded_identity_claim.size(),
        encoded_identity_claim_size));
    assert(encoded_identity_claim_size == expected_identity_claim.size());
    assert(std::equal(
        expected_identity_claim.begin(),
        expected_identity_claim.end(),
        encoded_identity_claim.begin()));

    PairingTranscript decoded = {};
    assert(decode_pairing_transcript(
        encoded.data(), encoded_size, decoded));
    assert(decoded == fixture());

    auto trailing = expected;
    std::array<uint8_t, expected.size() + 1> with_trailing = {};
    std::copy(trailing.begin(), trailing.end(), with_trailing.begin());
    assert(!decode_pairing_transcript(
        with_trailing.data(), with_trailing.size(), decoded));

    auto bad_version = expected;
    bad_version[6] = 2;
    assert(!decode_pairing_transcript(
        bad_version.data(), bad_version.size(), decoded));

    auto zero_nonce = expected;
    std::fill(zero_nonce.begin() + 31, zero_nonce.begin() + 47, 0);
    assert(!decode_pairing_transcript(
        zero_nonce.data(), zero_nonce.size(), decoded));

    auto noncanonical_key = expected;
    noncanonical_key[65] = 4;
    assert(!decode_pairing_transcript(
        noncanonical_key.data(), noncanonical_key.size(), decoded));

    PairingTranscript invalid = fixture();
    invalid.agent_name_size = kMaxAgentNameBytes + 1;
    assert(!encode_pairing_transcript(
        invalid, encoded.data(), encoded.size(), encoded_size));
    invalid = fixture();
    invalid.agent_name[3] = '\n';
    assert(!encode_pairing_transcript(
        invalid, encoded.data(), encoded.size(), encoded_size));
}
