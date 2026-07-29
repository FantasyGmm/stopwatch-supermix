#include "apps/app_codex_status/secure_pairing_wire.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <string>
#include <cstdint>
#include <cstring>

using namespace secure_pairing;

namespace {

uint8_t hex_nibble(char value)
{
    if (value >= '0' && value <= '9') {
        return static_cast<uint8_t>(value - '0');
    }
    return static_cast<uint8_t>(value - 'a' + 10);
}

template <size_t N>
std::array<uint8_t, N / 2> decode_hex(const char (&encoded)[N])
{
    static_assert(N % 2 == 1);
    std::array<uint8_t, N / 2> output = {};
    for (size_t index = 0; index < output.size(); ++index) {
        output[index] = static_cast<uint8_t>(
            (hex_nibble(encoded[index * 2]) << 4U) |
            hex_nibble(encoded[index * 2 + 1]));
    }
    return output;
}

void test_python_pairing_request_fixture_decodes_exactly()
{
    const auto encoded = decode_hex(
        "53575052010300000007005d"
        "4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e"
        "024545454545454545454545454545454545454545454545454545454545454545"
        "034949494949494949494949494949494949494949494949494949494949494949"
        "0a4465736b204167656e74");
    WireFrameView frame = {};
    assert(decode_wire_frame(encoded.data(), encoded.size(), frame));
    assert(frame.type == WireMessageType::PairingRequest);
    PairingRequest request = {};
    assert(decode_pairing_request(frame, 42, request));
    assert(request.connection_handle == 42);
    assert(request.transaction_id == 7);
    assert(request.agent_nonce[0] == 'N');
    assert(request.agent_ephemeral_public[0] == 0x02);
    assert(request.agent_identity_public[0] == 0x03);
    assert(request.agent_name_size == 10);
    assert(std::memcmp(request.agent_name.data(), "Desk Agent", 10) == 0);

    std::array<uint8_t, 12 + 83 + kMaxAgentNameBytes> max_name_frame = {};
    std::copy_n(encoded.begin(), 12 + 83, max_name_frame.begin());
    max_name_frame[11] = static_cast<uint8_t>(83 + kMaxAgentNameBytes);
    max_name_frame[12 + 82] = static_cast<uint8_t>(kMaxAgentNameBytes);
    std::fill_n(
        max_name_frame.begin() + 12 + 83,
        kMaxAgentNameBytes,
        static_cast<uint8_t>('A'));
    WireFrameView max_name_view = {};
    assert(decode_wire_frame(
        max_name_frame.data(), max_name_frame.size(), max_name_view));
    PairingRequest max_name_request = {};
    assert(decode_pairing_request(max_name_view, 8, max_name_request));
    assert(max_name_request.agent_name_size == kMaxAgentNameBytes);
    assert(std::all_of(
        max_name_request.agent_name.begin(),
        max_name_request.agent_name.end(),
        [](char value) { return value == 'A'; }));
}

void test_discovery_response_matches_python_fixture()
{
    WatchId watch_id = {};
    for (size_t index = 0; index < watch_id.size(); ++index) {
        watch_id[index] = static_cast<uint8_t>(index);
    }
    std::array<uint8_t, kMaxPairingWireFrameSize> encoded = {};
    size_t encoded_size = 0;
    assert(encode_discovery_response(
        1, watch_id, 7, encoded.data(), encoded.size(), encoded_size));
    const auto expected = decode_hex(
        "535750520102000000010014"
        "000102030405060708090a0b0c0d0e0f00000007");
    assert(encoded_size == expected.size());
    assert(std::equal(expected.begin(), expected.end(), encoded.begin()));
}

void test_python_authentication_request_fixture_decodes_exactly()
{
    const auto encoded = decode_hex(
        "53575052010800000009001f"
        "46464646464646464646464646464646000000020a"
        "53747564696f204d6163");
    WireFrameView frame = {};
    AuthenticationRequest request = {};
    assert(decode_wire_frame(encoded.data(), encoded.size(), frame));
    assert(decode_authentication_request(frame, 55, request));
    assert(request.connection_handle == 55);
    assert(request.transaction_id == 9);
    assert(request.agent_key_epoch == 2);
    assert(request.fingerprint[0] == 'F');
    assert(request.display_name_size == 10);
    assert(std::string(request.display_name.data()) == "Studio Mac");
}

void test_credential_envelope_and_proof_are_bounded()
{
    CredentialEnvelope envelope = {};
    envelope.purpose = CredentialPurpose::Rekey;
    envelope.transaction_id = 18;
    envelope.key_epoch = 6;
    envelope.watch_ephemeral_public[0] = 0x02;
    envelope.nonce.fill(0x4e);
    envelope.ack_nonce.fill(0x41);
    envelope.ciphertext_size = kCredentialCiphertextSize;
    envelope.ciphertext.fill(0x7c);
    std::array<uint8_t, kMaxPairingWireFrameSize> encoded = {};
    size_t encoded_size = 0;
    assert(encode_credential_envelope(
        envelope, encoded.data(), encoded.size(), encoded_size));
    assert(encoded_size <= kMaxPairingWireFrameSize);

    WireFrameView frame = {};
    assert(decode_wire_frame(encoded.data(), encoded_size, frame));
    assert(frame.type == WireMessageType::Credential);

    std::array<uint8_t, 72> signature = {};
    signature.fill(0x30);
    assert(encode_authentication_proof(
        18,
        signature.data(),
        signature.size(),
        encoded.data(),
        encoded.size(),
        encoded_size));
    assert(decode_wire_frame(encoded.data(), encoded_size, frame));
    const uint8_t* proof = nullptr;
    size_t proof_size = 0;
    assert(decode_authentication_proof(frame, proof, proof_size));
    assert(proof_size == signature.size());
}

}  // namespace

int main()
{
    test_python_pairing_request_fixture_decodes_exactly();
    test_discovery_response_matches_python_fixture();
    test_python_authentication_request_fixture_decodes_exactly();
    test_credential_envelope_and_proof_are_bounded();
    return 0;
}
