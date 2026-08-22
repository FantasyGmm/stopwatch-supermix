#include "codex_pet_protocol.h"

#include <algorithm>

namespace pet_protocol {
namespace {

constexpr size_t kTransactionBodyBytes = 4;
constexpr size_t kBeginBodyBytes = 48;
constexpr size_t kChunkHeaderBytes = 10;
constexpr size_t kStateBodyBytes = 48;
constexpr size_t kProgressBodyBytes = 12;
constexpr size_t kCommittedBodyBytes = 40;
constexpr size_t kErrorBodyBytes = 16;
constexpr size_t kMaxBodyBytes = kChunkHeaderBytes + kMaxChunkBytes;

uint16_t read_u16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8U);
}

uint32_t read_u32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) |
           (static_cast<uint32_t>(bytes[3]) << 24U);
}

void write_u16(uint8_t* bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value & 0xffU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
}

void write_u32(uint8_t* bytes, uint32_t value)
{
    bytes[0] = static_cast<uint8_t>(value & 0xffU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
    bytes[2] = static_cast<uint8_t>((value >> 16U) & 0xffU);
    bytes[3] = static_cast<uint8_t>((value >> 24U) & 0xffU);
}

bool nonzero(const voice_input::crypto::ExchangeNonce& nonce)
{
    return std::any_of(nonce.begin(), nonce.end(), [](uint8_t value) {
        return value != 0;
    });
}

template <size_t Size>
bool nonzero(const std::array<uint8_t, Size>& bytes)
{
    return std::any_of(bytes.begin(), bytes.end(), [](uint8_t value) {
        return value != 0;
    });
}

bool control_kind(Kind kind)
{
    switch (kind) {
        case Kind::Query:
        case Kind::Begin:
        case Kind::Chunk:
        case Kind::Commit:
        case Kind::Cancel:
        case Kind::Restore:
            return true;
        case Kind::State:
        case Kind::Progress:
        case Kind::Committed:
        case Kind::Cancelled:
        case Kind::Error:
            return false;
    }
    return false;
}

bool status_kind(Kind kind)
{
    switch (kind) {
        case Kind::State:
        case Kind::Progress:
        case Kind::Committed:
        case Kind::Cancelled:
        case Kind::Error:
            return true;
        case Kind::Query:
        case Kind::Begin:
        case Kind::Chunk:
        case Kind::Commit:
        case Kind::Cancel:
        case Kind::Restore:
            return false;
    }
    return false;
}

bool valid_source(Source source)
{
    return source == Source::BuiltIn || source == Source::Custom;
}

bool valid_reason(ErrorReason reason)
{
    const uint16_t raw = static_cast<uint16_t>(reason);
    return raw >= static_cast<uint16_t>(ErrorReason::InvalidMessage) &&
           raw <= static_cast<uint16_t>(ErrorReason::Internal);
}

bool valid_prefix(
    Kind kind,
    uint32_t sequence,
    const voice_input::crypto::ExchangeNonce& nonce)
{
    return sequence != 0 && nonzero(nonce) &&
           (control_kind(kind) || status_kind(kind));
}

bool valid_control(const Control& control)
{
    if (!control_kind(control.kind) ||
        !valid_prefix(control.kind, control.sequence, control.exchange_nonce) ||
        control.transaction_id == 0) {
        return false;
    }
    switch (control.kind) {
        case Kind::Begin:
            return control.schema != 0 && control.width != 0 &&
                   control.height != 0 && control.frame_count >= 1 &&
                   control.frame_count <= 24 && control.package_length >= 1 &&
                   control.package_length <= kMaxPackageBytes &&
                   nonzero(control.sha256);
        case Kind::Chunk:
            return control.data_size >= 1 &&
                   control.data_size <= kMaxChunkBytes &&
                   control.data_size <= UINT32_MAX - control.offset;
        case Kind::Query:
        case Kind::Commit:
        case Kind::Cancel:
        case Kind::Restore:
            return true;
        case Kind::State:
        case Kind::Progress:
        case Kind::Committed:
        case Kind::Cancelled:
        case Kind::Error:
            return false;
    }
    return false;
}

bool valid_status(const Status& status)
{
    if (!status_kind(status.kind) ||
        !valid_prefix(status.kind, status.sequence, status.exchange_nonce) ||
        status.transaction_id == 0) {
        return false;
    }
    switch (status.kind) {
        case Kind::State:
            return valid_source(status.source) && status.schema_min != 0 &&
                   status.schema_min <= status.schema_max &&
                   ((status.source == Source::BuiltIn &&
                     !nonzero(status.active_sha256)) ||
                    (status.source == Source::Custom &&
                     nonzero(status.active_sha256)));
        case Kind::Progress:
            return status.total_bytes >= 1 &&
                   status.total_bytes <= kMaxPackageBytes &&
                   status.acknowledged_bytes <= status.total_bytes;
        case Kind::Committed:
            return status.generation != 0 && nonzero(status.active_sha256);
        case Kind::Cancelled:
            return true;
        case Kind::Error:
            if (!valid_reason(status.reason)) {
                return false;
            }
            if (status.reason == ErrorReason::InsufficientStorage ||
                status.reason == ErrorReason::TooLarge) {
                return status.required_bytes > status.available_bytes;
            }
            return status.required_bytes == 0 && status.available_bytes == 0;
        case Kind::Query:
        case Kind::Begin:
        case Kind::Chunk:
        case Kind::Commit:
        case Kind::Cancel:
        case Kind::Restore:
            return false;
    }
    return false;
}

void write_prefix(
    uint8_t* output,
    Kind kind,
    uint32_t sequence,
    const voice_input::crypto::ExchangeNonce& nonce)
{
    output[0] = static_cast<uint8_t>(kind);
    output[1] = kProtocolVersion;
    write_u32(output + 2, sequence);
    std::copy(nonce.begin(), nonce.end(), output + 6);
}

bool parse_prefix(
    const uint8_t* bytes,
    size_t length,
    bool expect_control,
    Kind& kind,
    uint32_t& sequence,
    voice_input::crypto::ExchangeNonce& nonce)
{
    if (bytes == nullptr ||
        length < kPrefixBytes + kTransactionBodyBytes + voice_input::crypto::kTagSize ||
        length > kMaxFrameBytes || bytes[1] != kProtocolVersion) {
        return false;
    }
    kind = static_cast<Kind>(bytes[0]);
    sequence = read_u32(bytes + 2);
    std::copy_n(bytes + 6, nonce.size(), nonce.begin());
    return valid_prefix(kind, sequence, nonce) &&
           (expect_control ? control_kind(kind) : status_kind(kind));
}

size_t write_control_body(const Control& control, uint8_t* body)
{
    write_u32(body, control.transaction_id);
    switch (control.kind) {
        case Kind::Begin:
            write_u16(body + 4, control.schema);
            write_u16(body + 6, control.width);
            write_u16(body + 8, control.height);
            write_u16(body + 10, control.frame_count);
            write_u32(body + 12, control.package_length);
            std::copy(control.sha256.begin(), control.sha256.end(), body + 16);
            return kBeginBodyBytes;
        case Kind::Chunk:
            write_u32(body + 4, control.offset);
            write_u16(body + 8, static_cast<uint16_t>(control.data_size));
            std::copy_n(control.data.begin(), control.data_size, body + 10);
            return kChunkHeaderBytes + control.data_size;
        case Kind::Query:
        case Kind::Commit:
        case Kind::Cancel:
        case Kind::Restore:
            return kTransactionBodyBytes;
        case Kind::State:
        case Kind::Progress:
        case Kind::Committed:
        case Kind::Cancelled:
        case Kind::Error:
            return 0;
    }
    return 0;
}

size_t write_status_body(const Status& status, uint8_t* body)
{
    write_u32(body, status.transaction_id);
    switch (status.kind) {
        case Kind::State:
            body[4] = static_cast<uint8_t>(status.source);
            body[5] = status.usable ? 1 : 0;
            write_u16(body + 6, status.schema_min);
            write_u16(body + 8, status.schema_max);
            write_u16(body + 10, 0);
            write_u32(body + 12, status.generation);
            std::copy(
                status.active_sha256.begin(), status.active_sha256.end(), body + 16);
            return kStateBodyBytes;
        case Kind::Progress:
            write_u32(body + 4, status.acknowledged_bytes);
            write_u32(body + 8, status.total_bytes);
            return kProgressBodyBytes;
        case Kind::Committed:
            write_u32(body + 4, status.generation);
            std::copy(
                status.active_sha256.begin(), status.active_sha256.end(), body + 8);
            return kCommittedBodyBytes;
        case Kind::Cancelled:
            return kTransactionBodyBytes;
        case Kind::Error:
            write_u16(body + 4, static_cast<uint16_t>(status.reason));
            write_u16(body + 6, 0);
            write_u32(body + 8, status.required_bytes);
            write_u32(body + 12, status.available_bytes);
            return kErrorBodyBytes;
        case Kind::Query:
        case Kind::Begin:
        case Kind::Chunk:
        case Kind::Commit:
        case Kind::Cancel:
        case Kind::Restore:
            return 0;
    }
    return 0;
}

bool parse_control_body(
    Kind kind,
    const uint8_t* body,
    size_t body_size,
    Control& control)
{
    if (body == nullptr || body_size < kTransactionBodyBytes) {
        return false;
    }
    control.transaction_id = read_u32(body);
    switch (kind) {
        case Kind::Begin:
            if (body_size != kBeginBodyBytes) {
                return false;
            }
            control.schema = read_u16(body + 4);
            control.width = read_u16(body + 6);
            control.height = read_u16(body + 8);
            control.frame_count = read_u16(body + 10);
            control.package_length = read_u32(body + 12);
            std::copy_n(body + 16, control.sha256.size(), control.sha256.begin());
            return true;
        case Kind::Chunk: {
            if (body_size < kChunkHeaderBytes) {
                return false;
            }
            control.offset = read_u32(body + 4);
            control.data_size = read_u16(body + 8);
            if (control.data_size != body_size - kChunkHeaderBytes ||
                control.data_size > control.data.size()) {
                return false;
            }
            std::copy_n(body + 10, control.data_size, control.data.begin());
            return true;
        }
        case Kind::Query:
        case Kind::Commit:
        case Kind::Cancel:
        case Kind::Restore:
            return body_size == kTransactionBodyBytes;
        case Kind::State:
        case Kind::Progress:
        case Kind::Committed:
        case Kind::Cancelled:
        case Kind::Error:
            return false;
    }
    return false;
}

bool parse_status_body(
    Kind kind,
    const uint8_t* body,
    size_t body_size,
    Status& status)
{
    if (body == nullptr || body_size < kTransactionBodyBytes) {
        return false;
    }
    status.transaction_id = read_u32(body);
    switch (kind) {
        case Kind::State:
            if (body_size != kStateBodyBytes || read_u16(body + 10) != 0 ||
                body[5] > 1) {
                return false;
            }
            status.source = static_cast<Source>(body[4]);
            status.usable = body[5] == 1;
            status.schema_min = read_u16(body + 6);
            status.schema_max = read_u16(body + 8);
            status.generation = read_u32(body + 12);
            std::copy_n(
                body + 16, status.active_sha256.size(), status.active_sha256.begin());
            return true;
        case Kind::Progress:
            if (body_size != kProgressBodyBytes) {
                return false;
            }
            status.acknowledged_bytes = read_u32(body + 4);
            status.total_bytes = read_u32(body + 8);
            return true;
        case Kind::Committed:
            if (body_size != kCommittedBodyBytes) {
                return false;
            }
            status.generation = read_u32(body + 4);
            std::copy_n(
                body + 8, status.active_sha256.size(), status.active_sha256.begin());
            return true;
        case Kind::Cancelled:
            return body_size == kTransactionBodyBytes;
        case Kind::Error:
            if (body_size != kErrorBodyBytes || read_u16(body + 6) != 0) {
                return false;
            }
            status.reason = static_cast<ErrorReason>(read_u16(body + 4));
            status.required_bytes = read_u32(body + 8);
            status.available_bytes = read_u32(body + 12);
            return true;
        case Kind::Query:
        case Kind::Begin:
        case Kind::Chunk:
        case Kind::Commit:
        case Kind::Cancel:
        case Kind::Restore:
            return false;
    }
    return false;
}

}  // namespace

size_t encode_control(
    const Control& control,
    voice_input::crypto::CcmContext& crypto,
    uint8_t* output,
    size_t capacity)
{
    if (!valid_control(control) || output == nullptr) {
        return 0;
    }
    std::array<uint8_t, kMaxBodyBytes> body = {};
    const size_t body_size = write_control_body(control, body.data());
    const size_t frame_size = kPrefixBytes + body_size + voice_input::crypto::kTagSize;
    if (body_size == 0 || capacity < frame_size) {
        std::fill_n(body.data(), body_size, uint8_t{0});
        return 0;
    }
    write_prefix(
        output, control.kind, control.sequence, control.exchange_nonce);
    size_t encrypted_size = 0;
    const bool encrypted = crypto.encrypt(
            voice_input::crypto::Domain::PetControl,
            control.exchange_nonce,
            control.sequence,
            output,
            kPrefixBytes,
            body.data(),
            body_size,
            output + kPrefixBytes,
            capacity - kPrefixBytes,
            encrypted_size);
    std::fill_n(body.data(), body_size, uint8_t{0});
    if (!encrypted ||
        encrypted_size != body_size + voice_input::crypto::kTagSize) {
        return 0;
    }
    return frame_size;
}

bool decode_control(
    const uint8_t* bytes,
    size_t length,
    voice_input::crypto::CcmContext& crypto,
    Control& control)
{
    control = {};
    Kind kind = Kind::Query;
    uint32_t sequence = 0;
    voice_input::crypto::ExchangeNonce nonce = {};
    if (!parse_prefix(bytes, length, true, kind, sequence, nonce)) {
        return false;
    }
    std::array<uint8_t, kMaxBodyBytes> body = {};
    size_t body_size = 0;
    if (!crypto.decrypt(
            voice_input::crypto::Domain::PetControl,
            nonce,
            sequence,
            bytes,
            kPrefixBytes,
            bytes + kPrefixBytes,
            length - kPrefixBytes,
            body.data(),
            body.size(),
            body_size)) {
        return false;
    }
    Control candidate;
    candidate.kind = kind;
    candidate.sequence = sequence;
    candidate.exchange_nonce = nonce;
    if (!parse_control_body(kind, body.data(), body_size, candidate) ||
        !valid_control(candidate)) {
        std::fill_n(body.data(), body_size, uint8_t{0});
        return false;
    }
    std::fill_n(body.data(), body_size, uint8_t{0});
    control = candidate;
    return true;
}

size_t encode_status(
    const Status& status,
    voice_input::crypto::CcmContext& crypto,
    uint8_t* output,
    size_t capacity)
{
    if (!valid_status(status) || output == nullptr) {
        return 0;
    }
    std::array<uint8_t, kMaxBodyBytes> body = {};
    const size_t body_size = write_status_body(status, body.data());
    const size_t frame_size = kPrefixBytes + body_size + voice_input::crypto::kTagSize;
    if (body_size == 0 || capacity < frame_size) {
        std::fill_n(body.data(), body_size, uint8_t{0});
        return 0;
    }
    write_prefix(output, status.kind, status.sequence, status.exchange_nonce);
    size_t encrypted_size = 0;
    const bool encrypted = crypto.encrypt(
            voice_input::crypto::Domain::PetStatus,
            status.exchange_nonce,
            status.sequence,
            output,
            kPrefixBytes,
            body.data(),
            body_size,
            output + kPrefixBytes,
            capacity - kPrefixBytes,
            encrypted_size);
    std::fill_n(body.data(), body_size, uint8_t{0});
    if (!encrypted ||
        encrypted_size != body_size + voice_input::crypto::kTagSize) {
        return 0;
    }
    return frame_size;
}

bool decode_status(
    const uint8_t* bytes,
    size_t length,
    voice_input::crypto::CcmContext& crypto,
    Status& status)
{
    status = {};
    Kind kind = Kind::State;
    uint32_t sequence = 0;
    voice_input::crypto::ExchangeNonce nonce = {};
    if (!parse_prefix(bytes, length, false, kind, sequence, nonce)) {
        return false;
    }
    std::array<uint8_t, kMaxBodyBytes> body = {};
    size_t body_size = 0;
    if (!crypto.decrypt(
            voice_input::crypto::Domain::PetStatus,
            nonce,
            sequence,
            bytes,
            kPrefixBytes,
            bytes + kPrefixBytes,
            length - kPrefixBytes,
            body.data(),
            body.size(),
            body_size)) {
        return false;
    }
    Status candidate;
    candidate.kind = kind;
    candidate.sequence = sequence;
    candidate.exchange_nonce = nonce;
    if (!parse_status_body(kind, body.data(), body_size, candidate) ||
        !valid_status(candidate)) {
        std::fill_n(body.data(), body_size, uint8_t{0});
        return false;
    }
    std::fill_n(body.data(), body_size, uint8_t{0});
    status = candidate;
    return true;
}

void ControlReplayGuard::set_connection_generation(uint32_t generation)
{
    if (generation == _connection_generation) {
        return;
    }
    _connection_generation = generation;
    _active = false;
    _exchange_nonce = {};
    _transaction_id = 0;
    _last_sequence = 0;
    _next_offset = 0;
    _last_was_chunk = false;
    _last_chunk_frame_size = 0;
    _tombstone_count = 0;
    _tombstones = {};
}

bool ControlReplayGuard::begin(
    const voice_input::crypto::ExchangeNonce& exchange_nonce,
    uint32_t transaction_id)
{
    if (_connection_generation == 0 || _active || transaction_id == 0 ||
        !nonzero(exchange_nonce)) {
        return false;
    }
    for (size_t index = 0; index < _tombstone_count; ++index) {
        if (_tombstones[index].transaction_id == transaction_id ||
            _tombstones[index].exchange_nonce == exchange_nonce) {
            return false;
        }
    }
    _active = true;
    _exchange_nonce = exchange_nonce;
    _transaction_id = transaction_id;
    _last_sequence = 0;
    _next_offset = 0;
    _last_was_chunk = false;
    _last_chunk_sequence = 0;
    _last_chunk_offset = 0;
    _last_chunk_frame_size = 0;
    return true;
}

ReplayDecision ControlReplayGuard::accept(
    const Control& control,
    const uint8_t* frame,
    size_t frame_size)
{
    if (!_active || !valid_control(control) || frame == nullptr ||
        frame_size == 0 || frame_size > kMaxFrameBytes ||
        control.exchange_nonce != _exchange_nonce ||
        control.transaction_id != _transaction_id) {
        return ReplayDecision::Rejected;
    }
    if (control.sequence == _last_sequence) {
        if (control.kind == Kind::Chunk && _last_was_chunk &&
            control.sequence == _last_chunk_sequence &&
            control.offset == _last_chunk_offset &&
            frame_size == _last_chunk_frame_size &&
            std::equal(frame, frame + frame_size, _last_chunk_frame.begin())) {
            return ReplayDecision::Duplicate;
        }
        return ReplayDecision::Rejected;
    }
    if (control.sequence < _last_sequence) {
        return ReplayDecision::Rejected;
    }
    if (_last_sequence == 0 && control.sequence != 1) {
        return ReplayDecision::Rejected;
    }
    if (control.kind == Kind::Chunk) {
        if (control.offset != _next_offset ||
            control.data_size > UINT32_MAX - _next_offset) {
            return ReplayDecision::Rejected;
        }
        _next_offset += static_cast<uint32_t>(control.data_size);
        _last_was_chunk = true;
        _last_chunk_sequence = control.sequence;
        _last_chunk_offset = control.offset;
        _last_chunk_frame_size = frame_size;
        std::copy(frame, frame + frame_size, _last_chunk_frame.begin());
    } else {
        _last_was_chunk = false;
        _last_chunk_frame_size = 0;
    }
    _last_sequence = control.sequence;
    return ReplayDecision::Accepted;
}

bool ControlReplayGuard::conflicts(
    const voice_input::crypto::ExchangeNonce& exchange_nonce,
    uint32_t transaction_id) const
{
    if (_active &&
        (_transaction_id == transaction_id ||
         _exchange_nonce == exchange_nonce)) {
        return true;
    }
    for (size_t index = 0; index < _tombstone_count; ++index) {
        if (_tombstones[index].transaction_id == transaction_id ||
            _tombstones[index].exchange_nonce == exchange_nonce) {
            return true;
        }
    }
    return false;
}

void ControlReplayGuard::retire(bool completed)
{
    if (completed && _active) {
        Tombstone tombstone;
        tombstone.transaction_id = _transaction_id;
        tombstone.exchange_nonce = _exchange_nonce;
        if (_tombstone_count < _tombstones.size()) {
            _tombstones[_tombstone_count++] = tombstone;
        } else {
            std::move(
                _tombstones.begin() + 1,
                _tombstones.end(),
                _tombstones.begin());
            _tombstones.back() = tombstone;
        }
    }
    _active = false;
    _exchange_nonce = {};
    _transaction_id = 0;
    _last_sequence = 0;
    _next_offset = 0;
    _last_was_chunk = false;
    _last_chunk_frame_size = 0;
}

bool ControlReplayGuard::active() const
{
    return _active;
}

bool StatusReplayGuard::set_active_nonce(
    const voice_input::crypto::ExchangeNonce& exchange_nonce)
{
    if (_active || !nonzero(exchange_nonce)) {
        return false;
    }
    _active = true;
    _exchange_nonce = exchange_nonce;
    _last_sequence = 0;
    return true;
}

bool StatusReplayGuard::accept(const Status& status)
{
    if (!_active || !valid_status(status) ||
        status.exchange_nonce != _exchange_nonce ||
        status.sequence <= _last_sequence) {
        return false;
    }
    if (_last_sequence == 0 && status.sequence != 1) {
        return false;
    }
    _last_sequence = status.sequence;
    return true;
}

void StatusReplayGuard::retire()
{
    _active = false;
    _exchange_nonce = {};
    _last_sequence = 0;
}

}  // namespace pet_protocol
