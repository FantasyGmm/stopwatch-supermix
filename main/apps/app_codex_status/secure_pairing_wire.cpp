#include "secure_pairing_wire.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace secure_pairing {
namespace {

constexpr std::array<uint8_t, 4> kMagic = {'S', 'W', 'P', 'R'};
constexpr uint8_t kWireVersion = 1;
constexpr size_t kDiscoveryResponseSize = 53;
constexpr size_t kPairingRequestFixedSize = 83;
constexpr size_t kPairingOfferSize = 98;
constexpr size_t kAuthenticationRequestFixedSize = 21;
constexpr size_t kAuthenticationChallengeWireFixedSize = 71;
constexpr size_t kCredentialEnvelopeFixedSize = 66;

bool nonzero(const uint8_t* data, size_t size)
{
    return data != nullptr &&
           std::any_of(data, data + size, [](uint8_t value) {
               return value != 0;
           });
}

uint16_t read_u16(const uint8_t* data)
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(data[0]) << 8U) | data[1]);
}

uint32_t read_u32(const uint8_t* data)
{
    return (static_cast<uint32_t>(data[0]) << 24U) |
           (static_cast<uint32_t>(data[1]) << 16U) |
           (static_cast<uint32_t>(data[2]) << 8U) |
           data[3];
}

void write_u16(uint16_t value, uint8_t* output)
{
    output[0] = static_cast<uint8_t>((value >> 8U) & 0xffU);
    output[1] = static_cast<uint8_t>(value & 0xffU);
}

void write_u32(uint32_t value, uint8_t* output)
{
    output[0] = static_cast<uint8_t>((value >> 24U) & 0xffU);
    output[1] = static_cast<uint8_t>((value >> 16U) & 0xffU);
    output[2] = static_cast<uint8_t>((value >> 8U) & 0xffU);
    output[3] = static_cast<uint8_t>(value & 0xffU);
}

bool valid_type(uint8_t value)
{
    return value >= static_cast<uint8_t>(WireMessageType::DiscoveryRequest) &&
           value <= static_cast<uint8_t>(WireMessageType::Error);
}

bool encode_frame(
    WireMessageType type,
    uint32_t transaction_id,
    const uint8_t* payload,
    size_t payload_size,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size)
{
    output_size = 0;
    const size_t total = kPairingWireHeaderSize + payload_size;
    if (transaction_id == 0 || output == nullptr ||
        total > output_capacity || total > kMaxPairingWireFrameSize ||
        payload_size > 0xffffU ||
        (payload_size != 0 && payload == nullptr) ||
        !valid_type(static_cast<uint8_t>(type))) {
        return false;
    }
    std::copy(kMagic.begin(), kMagic.end(), output);
    output[4] = kWireVersion;
    output[5] = static_cast<uint8_t>(type);
    write_u32(transaction_id, output + 6);
    write_u16(static_cast<uint16_t>(payload_size), output + 10);
    if (payload_size != 0) {
        std::copy_n(payload, payload_size, output + kPairingWireHeaderSize);
    }
    output_size = total;
    return true;
}

}  // namespace

bool decode_wire_frame(
    const uint8_t* data,
    size_t size,
    WireFrameView& output)
{
    output = {};
    if (data == nullptr || size < kPairingWireHeaderSize ||
        size > kMaxPairingWireFrameSize ||
        !std::equal(kMagic.begin(), kMagic.end(), data) ||
        data[4] != kWireVersion || !valid_type(data[5])) {
        return false;
    }
    const uint32_t transaction_id = read_u32(data + 6);
    const size_t payload_size = read_u16(data + 10);
    if (transaction_id == 0 ||
        kPairingWireHeaderSize + payload_size != size) {
        return false;
    }
    output.type = static_cast<WireMessageType>(data[5]);
    output.transaction_id = transaction_id;
    output.payload = data + kPairingWireHeaderSize;
    output.payload_size = payload_size;
    return true;
}

bool encode_empty_wire_frame(
    WireMessageType type,
    uint32_t transaction_id,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size)
{
    if (type != WireMessageType::AuthenticationOk &&
        type != WireMessageType::Rejected &&
        type != WireMessageType::CredentialAckOk &&
        type != WireMessageType::Error) {
        return false;
    }
    return encode_frame(
        type,
        transaction_id,
        nullptr,
        0,
        output,
        output_capacity,
        output_size);
}

bool encode_discovery_response(
    uint32_t transaction_id,
    const WatchId& watch_id,
    uint32_t key_epoch,
    const crypto::PublicKey& watch_identity_public,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size)
{
    std::array<uint8_t, kDiscoveryResponseSize> payload = {};
    if (key_epoch == 0 || !nonzero(watch_id.data(), watch_id.size()) ||
        !nonzero(
            watch_identity_public.data(), watch_identity_public.size())) {
        return false;
    }
    std::copy(watch_id.begin(), watch_id.end(), payload.begin());
    write_u32(key_epoch, payload.data() + watch_id.size());
    std::copy(
        watch_identity_public.begin(),
        watch_identity_public.end(),
        payload.begin() + 20);
    return encode_frame(
        WireMessageType::DiscoveryResponse,
        transaction_id,
        payload.data(),
        payload.size(),
        output,
        output_capacity,
        output_size);
}

bool decode_pairing_request(
    const WireFrameView& frame,
    uint16_t connection_handle,
    PairingRequest& output)
{
    output = {};
    if (frame.type != WireMessageType::PairingRequest ||
        frame.payload == nullptr ||
        frame.payload_size < kPairingRequestFixedSize ||
        connection_handle == 0xffff) {
        return false;
    }
    const size_t name_size = frame.payload[82];
    const size_t signature_size_offset =
        kPairingRequestFixedSize + name_size;
    if (name_size == 0 || name_size > kMaxAgentNameBytes ||
        signature_size_offset + 2 > frame.payload_size) {
        return false;
    }
    const size_t signature_size =
        read_u16(frame.payload + signature_size_offset);
    if (signature_size == 0 || signature_size > crypto::kMaxSignatureSize ||
        signature_size_offset + 2 + signature_size != frame.payload_size) {
        return false;
    }
    output.connection_handle = connection_handle;
    output.transaction_id = frame.transaction_id;
    std::copy_n(frame.payload, 16, output.agent_nonce.begin());
    std::copy_n(
        frame.payload + 16, 33, output.agent_ephemeral_public.begin());
    std::copy_n(
        frame.payload + 49, 33, output.agent_identity_public.begin());
    std::copy_n(
        frame.payload + kPairingRequestFixedSize,
        name_size,
        output.agent_name.begin());
    output.agent_name_size = name_size;
    std::copy_n(
        frame.payload + signature_size_offset + 2,
        signature_size,
        output.identity_signature.begin());
    output.identity_signature_size = signature_size;
    return nonzero(output.agent_nonce.data(), output.agent_nonce.size()) &&
           nonzero(
               output.agent_ephemeral_public.data(),
               output.agent_ephemeral_public.size()) &&
           nonzero(
               output.agent_identity_public.data(),
               output.agent_identity_public.size()) &&
           nonzero(
               output.identity_signature.data(),
               output.identity_signature_size);
}

bool encode_pairing_offer(
    const PairingPresentation& presentation,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size)
{
    std::array<uint8_t, kPairingOfferSize> payload = {};
    std::copy(
        presentation.watch_id.begin(),
        presentation.watch_id.end(),
        payload.begin());
    std::copy(
        presentation.watch_nonce.begin(),
        presentation.watch_nonce.end(),
        payload.begin() + 16);
    std::copy(
        presentation.watch_ephemeral_public.begin(),
        presentation.watch_ephemeral_public.end(),
        payload.begin() + 32);
    std::copy(
        presentation.watch_identity_public.begin(),
        presentation.watch_identity_public.end(),
        payload.begin() + 65);
    return encode_frame(
        WireMessageType::PairingOffer,
        presentation.transaction_id,
        payload.data(),
        payload.size(),
        output,
        output_capacity,
        output_size);
}

bool decode_pairing_confirmation(
    const WireFrameView& frame,
    bool& accepted)
{
    if (frame.type != WireMessageType::PairingConfirm ||
        frame.payload_size != 1 || frame.payload == nullptr ||
        frame.payload[0] > 1) {
        return false;
    }
    accepted = frame.payload[0] == 1;
    return true;
}

bool decode_authentication_request(
    const WireFrameView& frame,
    uint16_t connection_handle,
    AuthenticationRequest& output)
{
    output = {};
    if (frame.type != WireMessageType::AuthenticationRequest ||
        frame.payload == nullptr ||
        frame.payload_size < kAuthenticationRequestFixedSize ||
        connection_handle == 0xffff) {
        return false;
    }
    output.connection_handle = connection_handle;
    output.transaction_id = frame.transaction_id;
    std::copy_n(frame.payload, 16, output.fingerprint.begin());
    output.agent_key_epoch = read_u32(frame.payload + 16);
    output.display_name_size = frame.payload[20];
    if (output.display_name_size == 0 ||
        output.display_name_size > kAgentNameBytes ||
        frame.payload_size !=
            kAuthenticationRequestFixedSize + output.display_name_size) {
        return false;
    }
    std::copy_n(
        frame.payload + kAuthenticationRequestFixedSize,
        output.display_name_size,
        output.display_name.begin());
    return output.agent_key_epoch != 0 &&
           nonzero(output.fingerprint.data(), output.fingerprint.size());
}

bool encode_authentication_challenge_wire(
    const AuthenticationChallenge& challenge,
    const uint8_t* watch_signature,
    size_t watch_signature_size,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size)
{
    std::array<uint8_t,
               kAuthenticationChallengeWireFixedSize + kAgentNameBytes + 2 +
                   crypto::kMaxSignatureSize>
        payload = {};
    if (challenge.key_epoch == 0 || challenge.connection_role != 1 ||
        challenge.display_name_size == 0 ||
        challenge.display_name_size > kAgentNameBytes ||
        watch_signature == nullptr || watch_signature_size == 0 ||
        watch_signature_size > crypto::kMaxSignatureSize) {
        return false;
    }
    std::copy(
        challenge.watch_id.begin(), challenge.watch_id.end(), payload.begin());
    std::copy(
        challenge.connection_nonce.begin(),
        challenge.connection_nonce.end(),
        payload.begin() + 16);
    write_u32(challenge.key_epoch, payload.data() + 32);
    payload[36] = challenge.connection_role;
    std::copy(
        challenge.watch_ephemeral_public.begin(),
        challenge.watch_ephemeral_public.end(),
        payload.begin() + 37);
    payload[70] = static_cast<uint8_t>(challenge.display_name_size);
    std::copy_n(
        challenge.display_name.begin(),
        challenge.display_name_size,
        payload.begin() + kAuthenticationChallengeWireFixedSize);
    const size_t signature_offset =
        kAuthenticationChallengeWireFixedSize + challenge.display_name_size;
    write_u16(static_cast<uint16_t>(watch_signature_size),
              payload.data() + signature_offset);
    std::copy_n(
        watch_signature,
        watch_signature_size,
        payload.begin() + static_cast<std::ptrdiff_t>(signature_offset + 2));
    return encode_frame(
        WireMessageType::AuthenticationChallenge,
        challenge.transaction_id,
        payload.data(),
        signature_offset + 2 + watch_signature_size,
        output,
        output_capacity,
        output_size);
}

bool encode_authentication_proof(
    uint32_t transaction_id,
    const uint8_t* signature,
    size_t signature_size,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size)
{
    if (signature == nullptr || signature_size == 0 || signature_size > 80) {
        return false;
    }
    std::array<uint8_t, 82> payload = {};
    write_u16(static_cast<uint16_t>(signature_size), payload.data());
    std::copy_n(signature, signature_size, payload.data() + 2);
    return encode_frame(
        WireMessageType::AuthenticationProof,
        transaction_id,
        payload.data(),
        signature_size + 2,
        output,
        output_capacity,
        output_size);
}

bool decode_authentication_proof(
    const WireFrameView& frame,
    const uint8_t*& signature,
    size_t& signature_size)
{
    signature = nullptr;
    signature_size = 0;
    if (frame.type != WireMessageType::AuthenticationProof ||
        frame.payload == nullptr || frame.payload_size < 3) {
        return false;
    }
    const size_t encoded_size = read_u16(frame.payload);
    if (encoded_size == 0 || encoded_size > 80 ||
        encoded_size + 2 != frame.payload_size) {
        return false;
    }
    signature = frame.payload + 2;
    signature_size = encoded_size;
    return true;
}

bool encode_credential_envelope(
    const CredentialEnvelope& envelope,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size)
{
    if ((envelope.purpose != CredentialPurpose::Pairing &&
         envelope.purpose != CredentialPurpose::Rekey) ||
        envelope.key_epoch == 0 || envelope.ciphertext_size == 0 ||
        envelope.ciphertext_size > envelope.ciphertext.size()) {
        return false;
    }
    std::array<uint8_t, kCredentialEnvelopeFixedSize +
                            kCredentialCiphertextSize>
        payload = {};
    payload[0] = static_cast<uint8_t>(envelope.purpose);
    write_u32(envelope.key_epoch, payload.data() + 1);
    std::copy(
        envelope.watch_ephemeral_public.begin(),
        envelope.watch_ephemeral_public.end(),
        payload.begin() + 5);
    std::copy(
        envelope.nonce.begin(), envelope.nonce.end(), payload.begin() + 38);
    std::copy(
        envelope.ack_nonce.begin(),
        envelope.ack_nonce.end(),
        payload.begin() + 51);
    write_u16(
        static_cast<uint16_t>(envelope.ciphertext_size), payload.data() + 64);
    std::copy_n(
        envelope.ciphertext.begin(),
        envelope.ciphertext_size,
        payload.begin() + kCredentialEnvelopeFixedSize);
    return encode_frame(
        WireMessageType::Credential,
        envelope.transaction_id,
        payload.data(),
        kCredentialEnvelopeFixedSize + envelope.ciphertext_size,
        output,
        output_capacity,
        output_size);
}

bool decode_credential_ack_wire(
    const WireFrameView& frame,
    const uint8_t*& proof,
    size_t& proof_size)
{
    proof = nullptr;
    proof_size = 0;
    if (frame.type != WireMessageType::CredentialAck ||
        frame.payload == nullptr || frame.payload_size < 3) {
        return false;
    }
    const size_t encoded_size = read_u16(frame.payload);
    if (encoded_size == 0 || encoded_size > 32 ||
        encoded_size + 2 != frame.payload_size) {
        return false;
    }
    proof = frame.payload + 2;
    proof_size = encoded_size;
    return true;
}

}  // namespace secure_pairing
