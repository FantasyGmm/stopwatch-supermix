#pragma once

#include "secure_pairing_crypto.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace secure_pairing {

constexpr uint8_t kPairingVersion = 1;
constexpr size_t kWatchIdSize = 16;
constexpr size_t kRandomSize = 16;
constexpr size_t kPublicKeySize = crypto::kPublicKeySize;
constexpr size_t kMaxAgentNameBytes = 24;
constexpr size_t kMaxTranscriptSize = 196;

using WatchId = std::array<uint8_t, kWatchIdSize>;
using RandomValue = std::array<uint8_t, kRandomSize>;

struct PairingTranscript {
    uint32_t transaction_id = 0;
    WatchId watch_id = {};
    RandomValue watch_nonce = {};
    RandomValue agent_nonce = {};
    crypto::PublicKey watch_ephemeral_public = {};
    crypto::PublicKey agent_ephemeral_public = {};
    crypto::PublicKey agent_identity_public = {};
    std::array<char, kMaxAgentNameBytes> agent_name = {};
    size_t agent_name_size = 0;
};

bool operator==(const PairingTranscript& lhs, const PairingTranscript& rhs);
bool operator!=(const PairingTranscript& lhs, const PairingTranscript& rhs);

bool encode_pairing_transcript(
    const PairingTranscript& value,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size);
bool decode_pairing_transcript(
    const uint8_t* input,
    size_t input_size,
    PairingTranscript& output);

}  // namespace secure_pairing
