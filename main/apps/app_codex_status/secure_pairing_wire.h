#pragma once

#include "secure_pairing_session.h"

#include <cstddef>
#include <cstdint>

namespace secure_pairing {

constexpr size_t kMaxPairingWireFrameSize = 256;
constexpr size_t kPairingWireHeaderSize = 12;

enum class WireMessageType : uint8_t {
    DiscoveryRequest = 1,
    DiscoveryResponse = 2,
    PairingRequest = 3,
    PairingOffer = 4,
    PairingConfirm = 5,
    Credential = 6,
    CredentialAck = 7,
    AuthenticationRequest = 8,
    AuthenticationChallenge = 9,
    AuthenticationProof = 10,
    AuthenticationOk = 11,
    Rejected = 12,
    CredentialAckOk = 13,
    Error = 14,
};

struct WireFrameView {
    WireMessageType type = WireMessageType::Error;
    uint32_t transaction_id = 0;
    const uint8_t* payload = nullptr;
    size_t payload_size = 0;
};

bool decode_wire_frame(
    const uint8_t* data,
    size_t size,
    WireFrameView& output);
bool encode_empty_wire_frame(
    WireMessageType type,
    uint32_t transaction_id,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size);
bool encode_discovery_response(
    uint32_t transaction_id,
    const WatchId& watch_id,
    uint32_t key_epoch,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size);
bool decode_pairing_request(
    const WireFrameView& frame,
    uint16_t connection_handle,
    PairingRequest& output);
bool encode_pairing_offer(
    const PairingPresentation& presentation,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size);
bool decode_pairing_confirmation(
    const WireFrameView& frame,
    bool& accepted);
bool decode_authentication_request(
    const WireFrameView& frame,
    uint16_t connection_handle,
    AuthenticationRequest& output);
bool encode_authentication_challenge_wire(
    const AuthenticationChallenge& challenge,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size);
bool encode_authentication_proof(
    uint32_t transaction_id,
    const uint8_t* signature,
    size_t signature_size,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size);
bool decode_authentication_proof(
    const WireFrameView& frame,
    const uint8_t*& signature,
    size_t& signature_size);
bool encode_credential_envelope(
    const CredentialEnvelope& envelope,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size);
bool decode_credential_ack_wire(
    const WireFrameView& frame,
    const uint8_t*& proof,
    size_t& proof_size);

}  // namespace secure_pairing
