#pragma once

#include "voice_input_crypto.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pet_protocol {

constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kPrefixBytes = 14;
constexpr size_t kMaxChunkBytes = 480;
constexpr uint32_t kMaxPackageBytes = 256U * 1024U;
constexpr size_t kMaxFrameBytes =
    kPrefixBytes + 4 + 4 + 2 + kMaxChunkBytes + voice_input::crypto::kTagSize;
constexpr size_t kCompletedTransactionTombstones = 8;

enum class Kind : uint8_t {
    Query = 0x01,
    Begin = 0x02,
    Chunk = 0x03,
    Commit = 0x04,
    Cancel = 0x05,
    Restore = 0x06,
    State = 0x81,
    Progress = 0x82,
    Committed = 0x83,
    Cancelled = 0x84,
    Error = 0xff,
};

enum class Source : uint8_t { BuiltIn = 0, Custom = 1 };

enum class ErrorReason : uint16_t {
    InvalidMessage = 1,
    Unauthorized = 2,
    Busy = 3,
    UnsupportedSchema = 4,
    InvalidMetadata = 5,
    InvalidOffset = 6,
    Replay = 7,
    InsufficientStorage = 8,
    HashMismatch = 9,
    IoFailure = 10,
    Timeout = 11,
    UnusablePackage = 12,
    TooLarge = 13,
    Internal = 14,
};

struct Control {
    Kind kind = Kind::Query;
    uint32_t sequence = 0;
    voice_input::crypto::ExchangeNonce exchange_nonce = {};
    uint32_t transaction_id = 0;

    uint16_t schema = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t frame_count = 0;
    uint32_t package_length = 0;
    std::array<uint8_t, 32> sha256 = {};

    uint32_t offset = 0;
    size_t data_size = 0;
    std::array<uint8_t, kMaxChunkBytes> data = {};
};

struct Status {
    Kind kind = Kind::State;
    uint32_t sequence = 0;
    voice_input::crypto::ExchangeNonce exchange_nonce = {};
    uint32_t transaction_id = 0;

    Source source = Source::BuiltIn;
    bool usable = false;
    uint16_t schema_min = 0;
    uint16_t schema_max = 0;
    uint32_t generation = 0;
    std::array<uint8_t, 32> active_sha256 = {};

    uint32_t acknowledged_bytes = 0;
    uint32_t total_bytes = 0;

    ErrorReason reason = ErrorReason::InvalidMessage;
    uint32_t required_bytes = 0;
    uint32_t available_bytes = 0;
};

size_t encode_control(
    const Control& control,
    voice_input::crypto::CcmContext& crypto,
    uint8_t* output,
    size_t capacity);
bool decode_control(
    const uint8_t* bytes,
    size_t length,
    voice_input::crypto::CcmContext& crypto,
    Control& control);
size_t encode_status(
    const Status& status,
    voice_input::crypto::CcmContext& crypto,
    uint8_t* output,
    size_t capacity);
bool decode_status(
    const uint8_t* bytes,
    size_t length,
    voice_input::crypto::CcmContext& crypto,
    Status& status);

enum class ReplayDecision : uint8_t {
    Rejected = 0,
    Accepted = 1,
    Duplicate = 2,
};

class ControlReplayGuard {
public:
    void set_connection_generation(uint32_t generation);
    bool begin(
        const voice_input::crypto::ExchangeNonce& exchange_nonce,
        uint32_t transaction_id);
    ReplayDecision accept(
        const Control& control,
        const uint8_t* frame,
        size_t frame_size);
    bool conflicts(
        const voice_input::crypto::ExchangeNonce& exchange_nonce,
        uint32_t transaction_id) const;
    void retire(bool completed);
    bool active() const;

private:
    struct Tombstone {
        uint32_t transaction_id = 0;
        voice_input::crypto::ExchangeNonce exchange_nonce = {};
    };

    uint32_t _connection_generation = 0;
    bool _active = false;
    voice_input::crypto::ExchangeNonce _exchange_nonce = {};
    uint32_t _transaction_id = 0;
    uint32_t _last_sequence = 0;
    uint32_t _next_offset = 0;
    bool _last_was_chunk = false;
    uint32_t _last_chunk_sequence = 0;
    uint32_t _last_chunk_offset = 0;
    size_t _last_chunk_frame_size = 0;
    std::array<uint8_t, kMaxFrameBytes> _last_chunk_frame = {};
    size_t _tombstone_count = 0;
    std::array<Tombstone, kCompletedTransactionTombstones> _tombstones = {};
};

class StatusReplayGuard {
public:
    bool set_active_nonce(
        const voice_input::crypto::ExchangeNonce& exchange_nonce);
    bool accept(const Status& status);
    void retire();

private:
    bool _active = false;
    voice_input::crypto::ExchangeNonce _exchange_nonce = {};
    uint32_t _last_sequence = 0;
};

}  // namespace pet_protocol
