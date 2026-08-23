#include "main/apps/app_codex_status/codex_pet_protocol.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

constexpr voice_input::crypto::Key kKey = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};
constexpr voice_input::crypto::ExchangeNonce kNonce = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
};
constexpr uint32_t kTransactionId = 0x01020304U;

Bytes hex(const char* text)
{
    Bytes output;
    for (size_t index = 0; text[index] != '\0'; index += 2) {
        const auto nibble = [](char value) -> uint8_t {
            return value <= '9' ? static_cast<uint8_t>(value - '0')
                                : static_cast<uint8_t>(value - 'a' + 10);
        };
        output.push_back(static_cast<uint8_t>(
            (nibble(text[index]) << 4U) | nibble(text[index + 1])));
    }
    return output;
}

const Bytes kQueryVector = hex(
    "01010100000010111213141516175d3d462fa46f7bf1d0d92e73");
const Bytes kBeginVector = hex(
    "020102000000101112131415161788bb7e49c9531426e10fc123bb3476a5e"
    "a13598634074dda1ccef5ed97414ed5cb8d63499bd23530b0c07f3feeb23a"
    "584009d1eb13b56f25");
const Bytes kChunkVector = hex(
    "0301030000001011121314151617701326bf823da2698cb7df26e8f0ea1ca"
    "1211964d5");
const Bytes kCommitVector = hex(
    "04010400000010111213141516177cf7aeb3f23f240b4981d67c");
const Bytes kCancelVector = hex(
    "050105000000101112131415161757979d3d6216e3f6a769fcee");
const Bytes kRestoreVector = hex(
    "0601060000001011121314151617e69e3b934aa0b056d3477216");
const Bytes kStateVector = hex(
    "8101010000001011121314151617e9d2d655323df2382eb2570a831baa8db"
    "30b762c0779d100463f1eff70bad738e123d2a36cb55ec903c43d1d4c8da4"
    "4e7f598868b457e7d6");
const Bytes kProgressVector = hex(
    "8201020000001011121314151617dbe2bbbd570b4401b443926883221064ff"
    "5428a1");
const Bytes kCommittedVector = hex(
    "8301030000001011121314151617ae9330e69e6cbe3aadd43061ce05c6929"
    "8a4b194842272010255eac6d361c3d1ad4dbd8f2c3a9bfe56ee2b4bf3329"
    "da4");
const Bytes kCancelledVector = hex(
    "8401040000001011121314151617d128ce47b50fd1f1092bc4c3");
const Bytes kErrorVector = hex(
    "ff01050000001011121314151617528b1cf8a2cb5df7b85c0005b9d62c613"
    "19c8a559d896cc1");

template <typename T>
Bytes encode_control(const T& control, voice_input::crypto::CcmContext& crypto)
{
    std::array<uint8_t, pet_protocol::kMaxFrameBytes> output = {};
    const size_t size = pet_protocol::encode_control(
        control, crypto, output.data(), output.size());
    assert(size != 0);
    return Bytes(output.begin(), output.begin() + size);
}

Bytes encode_status(
    const pet_protocol::Status& status,
    voice_input::crypto::CcmContext& crypto)
{
    std::array<uint8_t, pet_protocol::kMaxFrameBytes> output = {};
    const size_t size = pet_protocol::encode_status(
        status, crypto, output.data(), output.size());
    assert(size != 0);
    return Bytes(output.begin(), output.begin() + size);
}

void write_u16(Bytes& bytes, size_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value & 0xffU);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
}

void write_u32(Bytes& bytes, size_t offset, uint32_t value)
{
    bytes[offset] = static_cast<uint8_t>(value & 0xffU);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 2] = static_cast<uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 3] = static_cast<uint8_t>((value >> 24U) & 0xffU);
}

Bytes authenticated_frame(
    pet_protocol::Kind kind,
    uint32_t sequence,
    voice_input::crypto::Domain domain,
    const Bytes& body,
    voice_input::crypto::CcmContext& crypto)
{
    Bytes frame(pet_protocol::kPrefixBytes + body.size() + voice_input::crypto::kTagSize);
    frame[0] = static_cast<uint8_t>(kind);
    frame[1] = pet_protocol::kProtocolVersion;
    write_u32(frame, 2, sequence);
    std::copy(kNonce.begin(), kNonce.end(), frame.begin() + 6);
    size_t encrypted_size = 0;
    assert(crypto.encrypt(
        domain,
        kNonce,
        sequence,
        frame.data(),
        pet_protocol::kPrefixBytes,
        body.data(),
        body.size(),
        frame.data() + pet_protocol::kPrefixBytes,
        frame.size() - pet_protocol::kPrefixBytes,
        encrypted_size));
    assert(encrypted_size == body.size() + voice_input::crypto::kTagSize);
    return frame;
}

}  // namespace

int main()
{
    using namespace pet_protocol;
    static_assert(static_cast<uint8_t>(Kind::Query) == 0x01);
    static_assert(static_cast<uint8_t>(Kind::Begin) == 0x02);
    static_assert(static_cast<uint8_t>(Kind::Chunk) == 0x03);
    static_assert(static_cast<uint8_t>(Kind::Commit) == 0x04);
    static_assert(static_cast<uint8_t>(Kind::Cancel) == 0x05);
    static_assert(static_cast<uint8_t>(Kind::Restore) == 0x06);
    static_assert(static_cast<uint8_t>(Kind::State) == 0x81);
    static_assert(static_cast<uint8_t>(Kind::Progress) == 0x82);
    static_assert(static_cast<uint8_t>(Kind::Committed) == 0x83);
    static_assert(static_cast<uint8_t>(Kind::Cancelled) == 0x84);
    static_assert(static_cast<uint8_t>(Kind::Error) == 0xff);
    static_assert(static_cast<uint8_t>(Source::BuiltIn) == 0);
    static_assert(static_cast<uint8_t>(Source::Custom) == 1);
    static_assert(static_cast<uint16_t>(ErrorReason::UnsupportedSchema) == 4);
    static_assert(kCompletedTransactionTombstones == 8);

    voice_input::crypto::CcmContext crypto(kKey);

    Control query;
    query.kind = Kind::Query;
    query.sequence = 1;
    query.exchange_nonce = kNonce;
    query.transaction_id = kTransactionId;
    assert(encode_control(query, crypto) == kQueryVector);

    Control begin;
    begin.kind = Kind::Begin;
    begin.sequence = 2;
    begin.exchange_nonce = kNonce;
    begin.transaction_id = kTransactionId;
    begin.schema = 1;
    begin.width = 144;
    begin.height = 156;
    begin.frame_count = 2;
    begin.package_length = 0x1234;
    for (size_t index = 0; index < begin.sha256.size(); ++index) {
        begin.sha256[index] = static_cast<uint8_t>(index);
    }
    assert(encode_control(begin, crypto) == kBeginVector);

    Control chunk;
    chunk.kind = Kind::Chunk;
    chunk.sequence = 3;
    chunk.exchange_nonce = kNonce;
    chunk.transaction_id = kTransactionId;
    chunk.offset = 0x20;
    chunk.data_size = 3;
    chunk.data[0] = 'a';
    chunk.data[1] = 'b';
    chunk.data[2] = 'c';
    assert(encode_control(chunk, crypto) == kChunkVector);

    for (const auto& pair : {
             std::pair<Kind, const Bytes*>{Kind::Commit, &kCommitVector},
             {Kind::Cancel, &kCancelVector},
         }) {
        Control terminal;
        terminal.kind = pair.first;
        terminal.sequence = pair.first == Kind::Commit ? 4 : 5;
        terminal.exchange_nonce = kNonce;
        terminal.transaction_id = kTransactionId;
        assert(encode_control(terminal, crypto) == *pair.second);
    }
    Control restore;
    restore.kind = Kind::Restore;
    restore.sequence = 6;
    restore.exchange_nonce = kNonce;
    restore.transaction_id = 0x0a0b0c0dU;
    assert(encode_control(restore, crypto) == kRestoreVector);

    Control decoded_control;
    assert(decode_control(kBeginVector.data(), kBeginVector.size(), crypto, decoded_control));
    assert(decoded_control.kind == Kind::Begin);
    assert(decoded_control.sequence == 2);
    assert(decoded_control.exchange_nonce == kNonce);
    assert(decoded_control.transaction_id == kTransactionId);
    assert(decoded_control.schema == 1);
    assert(decoded_control.width == 144);
    assert(decoded_control.height == 156);
    assert(decoded_control.frame_count == 2);
    assert(decoded_control.package_length == 0x1234);
    assert(decode_control(kChunkVector.data(), kChunkVector.size(), crypto, decoded_control));
    assert(decoded_control.offset == 0x20);
    assert(decoded_control.data_size == 3);
    assert(decoded_control.data[0] == 'a' && decoded_control.data[2] == 'c');

    Status state;
    state.kind = Kind::State;
    state.sequence = 1;
    state.exchange_nonce = kNonce;
    state.transaction_id = kTransactionId;
    state.source = Source::Custom;
    state.usable = true;
    state.schema_min = 1;
    state.schema_max = 1;
    state.generation = 7;
    for (size_t index = 0; index < state.active_sha256.size(); ++index) {
        state.active_sha256[index] = static_cast<uint8_t>(0x20 + index);
    }
    assert(encode_status(state, crypto) == kStateVector);

    Status progress;
    progress.kind = Kind::Progress;
    progress.sequence = 2;
    progress.exchange_nonce = kNonce;
    progress.transaction_id = kTransactionId;
    progress.acknowledged_bytes = 3;
    progress.total_bytes = 0x1234;
    assert(encode_status(progress, crypto) == kProgressVector);

    Status committed;
    committed.kind = Kind::Committed;
    committed.sequence = 3;
    committed.exchange_nonce = kNonce;
    committed.transaction_id = kTransactionId;
    committed.generation = 8;
    committed.active_sha256 = state.active_sha256;
    assert(encode_status(committed, crypto) == kCommittedVector);

    Status cancelled;
    cancelled.kind = Kind::Cancelled;
    cancelled.sequence = 4;
    cancelled.exchange_nonce = kNonce;
    cancelled.transaction_id = kTransactionId;
    assert(encode_status(cancelled, crypto) == kCancelledVector);

    Status error;
    error.kind = Kind::Error;
    error.sequence = 5;
    error.exchange_nonce = kNonce;
    error.transaction_id = kTransactionId;
    error.reason = ErrorReason::UnsupportedSchema;
    error.required_bytes = 0;
    error.available_bytes = 0;
    assert(encode_status(error, crypto) == kErrorVector);

    Status decoded_status;
    assert(decode_status(kStateVector.data(), kStateVector.size(), crypto, decoded_status));
    assert(decoded_status.kind == Kind::State);
    assert(decoded_status.source == Source::Custom);
    assert(decoded_status.usable);
    assert(decoded_status.schema_min == 1 && decoded_status.schema_max == 1);
    assert(decoded_status.generation == 7);
    assert(decoded_status.active_sha256 == state.active_sha256);
    assert(decode_status(kErrorVector.data(), kErrorVector.size(), crypto, decoded_status));
    assert(decoded_status.reason == ErrorReason::UnsupportedSchema);
    assert(decoded_status.required_bytes == 0);
    assert(decoded_status.available_bytes == 0);

    auto wrong_key = kKey;
    wrong_key[0] ^= 1;
    voice_input::crypto::CcmContext wrong_crypto(wrong_key);
    assert(!decode_control(kChunkVector.data(), kChunkVector.size(), wrong_crypto, decoded_control));
    for (const size_t index : {size_t{0}, size_t{1}, size_t{2}, size_t{6}, kChunkVector.size() - 1}) {
        auto changed = kChunkVector;
        changed[index] ^= 1;
        assert(!decode_control(changed.data(), changed.size(), crypto, decoded_control));
    }
    auto zero_nonce = kQueryVector;
    std::fill(zero_nonce.begin() + 6, zero_nonce.begin() + 14, 0);
    assert(!decode_control(zero_nonce.data(), zero_nonce.size(), crypto, decoded_control));
    auto zero_sequence = kQueryVector;
    std::fill(zero_sequence.begin() + 2, zero_sequence.begin() + 6, 0);
    assert(!decode_control(zero_sequence.data(), zero_sequence.size(), crypto, decoded_control));
    assert(!decode_control(kChunkVector.data(), kChunkVector.size() - 1, crypto, decoded_control));
    assert(!decode_control(nullptr, kChunkVector.size(), crypto, decoded_control));

    Control oversized = chunk;
    oversized.data_size = kMaxChunkBytes + 1;
    std::array<uint8_t, kMaxFrameBytes> output = {};
    assert(pet_protocol::encode_control(oversized, crypto, output.data(), output.size()) == 0);
    chunk.data_size = 0;
    assert(pet_protocol::encode_control(chunk, crypto, output.data(), output.size()) == 0);
    begin.frame_count = 25;
    assert(pet_protocol::encode_control(begin, crypto, output.data(), output.size()) == 0);
    begin.frame_count = 2;
    begin.package_length = kMaxPackageBytes + 1;
    assert(pet_protocol::encode_control(begin, crypto, output.data(), output.size()) == 0);
    Control maximum = query;
    maximum.sequence = UINT32_MAX;
    assert(pet_protocol::encode_control(maximum, crypto, output.data(), output.size()) != 0);
    ++maximum.sequence;
    assert(maximum.sequence == 0);
    assert(pet_protocol::encode_control(maximum, crypto, output.data(), output.size()) == 0);

    Status invalid = state;
    invalid.source = static_cast<Source>(2);
    assert(pet_protocol::encode_status(invalid, crypto, output.data(), output.size()) == 0);
    invalid = state;
    invalid.schema_min = 2;
    invalid.schema_max = 1;
    assert(pet_protocol::encode_status(invalid, crypto, output.data(), output.size()) == 0);
    invalid = progress;
    invalid.acknowledged_bytes = invalid.total_bytes + 1;
    assert(pet_protocol::encode_status(invalid, crypto, output.data(), output.size()) == 0);
    invalid = error;
    invalid.reason = static_cast<ErrorReason>(15);
    assert(pet_protocol::encode_status(invalid, crypto, output.data(), output.size()) == 0);
    invalid = error;
    invalid.required_bytes = 2;
    invalid.available_bytes = 1;
    assert(pet_protocol::encode_status(invalid, crypto, output.data(), output.size()) == 0);
    invalid = error;
    invalid.reason = ErrorReason::InsufficientStorage;
    invalid.required_bytes = 10;
    invalid.available_bytes = 10;
    assert(pet_protocol::encode_status(invalid, crypto, output.data(), output.size()) == 0);
    invalid.available_bytes = 9;
    assert(pet_protocol::encode_status(invalid, crypto, output.data(), output.size()) != 0);

    auto changed_reserved = kStateVector;
    changed_reserved[14 + 10] ^= 1;
    assert(!decode_status(changed_reserved.data(), changed_reserved.size(), crypto, decoded_status));
    auto changed_reason = kErrorVector;
    changed_reason[14 + 4] = 15;
    assert(!decode_status(changed_reason.data(), changed_reason.size(), crypto, decoded_status));

    Bytes malformed_state_body(48, 0);
    write_u32(malformed_state_body, 0, kTransactionId);
    malformed_state_body[4] = static_cast<uint8_t>(Source::Custom);
    malformed_state_body[5] = 1;
    write_u16(malformed_state_body, 6, 1);
    write_u16(malformed_state_body, 8, 1);
    write_u16(malformed_state_body, 10, 1);
    write_u32(malformed_state_body, 12, 7);
    std::copy(state.active_sha256.begin(), state.active_sha256.end(), malformed_state_body.begin() + 16);
    const Bytes authenticated_bad_state = authenticated_frame(
        Kind::State,
        1,
        voice_input::crypto::Domain::PetStatus,
        malformed_state_body,
        crypto);
    assert(!decode_status(
        authenticated_bad_state.data(), authenticated_bad_state.size(), crypto, decoded_status));

    Bytes malformed_error_body(16, 0);
    write_u32(malformed_error_body, 0, kTransactionId);
    write_u16(malformed_error_body, 4, 15);
    write_u32(malformed_error_body, 8, 2);
    write_u32(malformed_error_body, 12, 1);
    const Bytes authenticated_bad_error = authenticated_frame(
        Kind::Error,
        5,
        voice_input::crypto::Domain::PetStatus,
        malformed_error_body,
        crypto);
    assert(!decode_status(
        authenticated_bad_error.data(), authenticated_bad_error.size(), crypto, decoded_status));

    Bytes query_body(4, 0);
    write_u32(query_body, 0, kTransactionId);
    const Bytes wrong_domain = authenticated_frame(
        Kind::Query,
        1,
        voice_input::crypto::Domain::PetStatus,
        query_body,
        crypto);
    assert(!decode_control(wrong_domain.data(), wrong_domain.size(), crypto, decoded_control));
    Bytes malformed_chunk_body(13, 0);
    write_u32(malformed_chunk_body, 0, kTransactionId);
    write_u32(malformed_chunk_body, 4, 0x20);
    write_u16(malformed_chunk_body, 8, 4);
    malformed_chunk_body[10] = 'a';
    malformed_chunk_body[11] = 'b';
    malformed_chunk_body[12] = 'c';
    const Bytes authenticated_bad_chunk = authenticated_frame(
        Kind::Chunk,
        3,
        voice_input::crypto::Domain::PetControl,
        malformed_chunk_body,
        crypto);
    assert(!decode_control(
        authenticated_bad_chunk.data(), authenticated_bad_chunk.size(), crypto, decoded_control));

    ControlReplayGuard skipped_sequence_guard;
    skipped_sequence_guard.set_connection_generation(1);
    assert(skipped_sequence_guard.begin(kNonce, kTransactionId));
    Control skipped = query;
    skipped.sequence = 2;
    const Bytes skipped_frame = encode_control(skipped, crypto);
    assert(skipped_sequence_guard.accept(
               skipped, skipped_frame.data(), skipped_frame.size()) ==
           ReplayDecision::Rejected);

    ControlReplayGuard guard;
    guard.set_connection_generation(4);
    assert(guard.begin(kNonce, kTransactionId));
    Control replay_chunk = chunk;
    replay_chunk.sequence = 1;
    replay_chunk.offset = 0;
    replay_chunk.data_size = 3;
    replay_chunk.data[0] = 'a';
    replay_chunk.data[1] = 'b';
    replay_chunk.data[2] = 'c';
    const Bytes replay_frame = encode_control(replay_chunk, crypto);
    assert(guard.accept(replay_chunk, replay_frame.data(), replay_frame.size()) == ReplayDecision::Accepted);
    assert(guard.accept(replay_chunk, replay_frame.data(), replay_frame.size()) == ReplayDecision::Duplicate);
    auto conflict = replay_frame;
    conflict.back() ^= 1;
    assert(guard.accept(replay_chunk, conflict.data(), conflict.size()) == ReplayDecision::Rejected);
    Control conflicting_chunk = replay_chunk;
    conflicting_chunk.data[2] = 'd';
    const Bytes conflicting_frame = encode_control(conflicting_chunk, crypto);
    assert(guard.accept(
               conflicting_chunk,
               conflicting_frame.data(),
               conflicting_frame.size()) == ReplayDecision::Rejected);

    Control gap = replay_chunk;
    gap.sequence = 2;
    gap.offset = 4;
    gap.data_size = 1;
    gap.data[0] = 'x';
    const Bytes gap_frame = encode_control(gap, crypto);
    assert(guard.accept(gap, gap_frame.data(), gap_frame.size()) == ReplayDecision::Rejected);
    gap.offset = 3;
    const Bytes next_frame = encode_control(gap, crypto);
    assert(guard.accept(gap, next_frame.data(), next_frame.size()) == ReplayDecision::Accepted);
    guard.retire(true);
    assert(!guard.begin(kNonce, kTransactionId));
    guard.set_connection_generation(5);
    assert(guard.begin(kNonce, kTransactionId));
    guard.retire(false);

    ControlReplayGuard bounded;
    bounded.set_connection_generation(8);
    for (uint32_t transaction = 1; transaction <= 9; ++transaction) {
        voice_input::crypto::ExchangeNonce nonce = {};
        nonce[0] = static_cast<uint8_t>(transaction);
        assert(bounded.begin(nonce, transaction));
        bounded.retire(true);
    }
    voice_input::crypto::ExchangeNonce first_nonce = {};
    first_nonce[0] = 1;
    assert(bounded.begin(first_nonce, 1));

    ControlReplayGuard reused_nonce;
    reused_nonce.set_connection_generation(1);
    assert(reused_nonce.begin(kNonce, 1));
    reused_nonce.retire(true);
    assert(!reused_nonce.begin(kNonce, 2));

    StatusReplayGuard status_guard;
    StatusReplayGuard skipped_status_guard;
    assert(skipped_status_guard.set_active_nonce(kNonce));
    assert(!skipped_status_guard.accept(progress));
    assert(!status_guard.accept(state));
    assert(status_guard.set_active_nonce(kNonce));
    assert(status_guard.accept(state));
    assert(!status_guard.accept(state));
    assert(status_guard.accept(progress));
    status_guard.retire();
    assert(!status_guard.accept(progress));

    return 0;
}
