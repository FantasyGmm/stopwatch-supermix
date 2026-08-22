#include "main/apps/app_codex_status/codex_pet_transfer.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr voice_input::crypto::Key kKey = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};
constexpr uint32_t kConnectionGeneration = 7;
constexpr uint32_t kKeyEpoch = 11;
constexpr uint64_t kSubscriptionGeneration = 1;

uint8_t hex_nibble(char value)
{
    assert(std::isxdigit(static_cast<unsigned char>(value)) != 0);
    return value <= '9' ? static_cast<uint8_t>(value - '0')
                        : static_cast<uint8_t>(value - 'a' + 10);
}

std::vector<uint8_t> read_hex(const char* path)
{
    std::ifstream input(path);
    assert(input.good());
    const std::string text(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    std::string hex;
    for (const char value : text) {
        if (std::isspace(static_cast<unsigned char>(value)) == 0) {
            hex.push_back(value);
        }
    }
    assert(!hex.empty() && (hex.size() % 2U) == 0U);
    std::vector<uint8_t> bytes(hex.size() / 2U);
    for (size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<uint8_t>(
            (hex_nibble(hex[index * 2U]) << 4U) |
            hex_nibble(hex[index * 2U + 1U]));
    }
    return bytes;
}

uint16_t read_u16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0]) |
           static_cast<uint16_t>(bytes[1]) << 8U;
}

codex_pet::PetMetadata metadata(const std::vector<uint8_t>& package)
{
    codex_pet::PetMetadata result{};
    result.schema = read_u16(package.data() + 4);
    result.width = read_u16(package.data() + 8);
    result.height = read_u16(package.data() + 10);
    result.frame_count = read_u16(package.data() + 12);
    std::copy_n(package.data() + 40, result.hash.size(), result.hash.begin());
    return result;
}

class FakeFileSystem final : public codex_pet::PetFileSystem {
public:
    bool ensure_directory() override { return true; }

    bool free_bytes(uint64_t& output) const override
    {
        output = available;
        return true;
    }

    bool open_staging(uint8_t slot) override
    {
        if (slot > 1) {
            return false;
        }
        files[slot].clear();
        present[slot] = true;
        open[slot] = true;
        return true;
    }

    bool write_at(
        uint8_t slot,
        uint32_t offset,
        codex_pet::ByteView bytes) override
    {
        if (slot > 1 || !open[slot] || bytes.data == nullptr ||
            bytes.size > std::numeric_limits<size_t>::max() - offset) {
            return false;
        }
        const size_t end = static_cast<size_t>(offset) + bytes.size;
        files[slot].resize(std::max(files[slot].size(), end));
        std::copy_n(bytes.data, bytes.size, files[slot].begin() + offset);
        return true;
    }

    bool read_at(
        uint8_t slot,
        uint32_t offset,
        codex_pet::MutableByteView output) const override
    {
        if (slot > 1 || output.data == nullptr || !present[slot] ||
            offset > files[slot].size() ||
            output.size > files[slot].size() - offset) {
            return false;
        }
        std::copy_n(files[slot].data() + offset, output.size, output.data);
        return true;
    }

    bool flush(uint8_t slot) override { return slot < 2 && open[slot]; }
    bool sync(uint8_t slot) override { return slot < 2 && open[slot]; }

    bool close(uint8_t slot) override
    {
        if (slot > 1 || !open[slot]) {
            return false;
        }
        open[slot] = false;
        return true;
    }

    std::optional<size_t> file_size(uint8_t slot) const override
    {
        if (slot > 1 || !present[slot]) {
            return std::nullopt;
        }
        return files[slot].size();
    }

    bool remove(uint8_t slot) override
    {
        if (slot > 1 || fail_cleanup) {
            return false;
        }
        files[slot].clear();
        present[slot] = false;
        open[slot] = false;
        return true;
    }

    uint64_t available = std::numeric_limits<uint64_t>::max();
    bool fail_cleanup = false;
    std::array<std::vector<uint8_t>, 2> files{};
    std::array<bool, 2> present{};
    std::array<bool, 2> open{};
};

class FakeAllocator final : public codex_pet::PetBufferAllocator {
public:
    uint8_t* allocate(size_t bytes) override
    {
        storage.resize(bytes);
        return storage.data();
    }

    void release(uint8_t* buffer) override
    {
        assert(buffer == storage.data());
    }

    std::vector<uint8_t> storage{};
};

class FakeSelectionStore final : public codex_pet::PetSelectionStore {
public:
    codex_pet::SelectionIoResult load(
        codex_pet::SelectionRecord& output) const override
    {
        if (!persistent.has_value()) {
            return codex_pet::SelectionIoResult::NotFound;
        }
        output = *persistent;
        return codex_pet::SelectionIoResult::Ok;
    }

    bool stage(const codex_pet::SelectionRecord& record) override
    {
        pending = record;
        return true;
    }

    bool commit() override
    {
        if (!pending.has_value()) {
            return false;
        }
        persistent = pending;
        pending.reset();
        return true;
    }

    std::optional<codex_pet::SelectionRecord> persistent{};
    std::optional<codex_pet::SelectionRecord> pending{};
};

struct Rig {
    FakeFileSystem files{};
    FakeAllocator allocator{};
    FakeSelectionStore selections{};
    codex_pet::CodexPetStore store{files, allocator, selections};
    voice_input::crypto::CcmContext crypto{kKey};
    codex_pet::CodexPetTransfer transfer{store, crypto};
};

voice_input::crypto::ExchangeNonce nonce(uint8_t seed)
{
    voice_input::crypto::ExchangeNonce result{};
    for (size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<uint8_t>(seed + index);
    }
    return result;
}

CodexPetControlFrame frame_for(
    const pet_protocol::Control& control,
    voice_input::crypto::CcmContext& crypto,
    uint32_t connection_generation = kConnectionGeneration,
    uint32_t key_epoch = kKeyEpoch,
    bool authorized = true,
    bool subscribed = true,
    uint64_t subscription_generation = kSubscriptionGeneration)
{
    CodexPetControlFrame frame{};
    frame.connection_handle = 19;
    frame.connection_generation = connection_generation;
    frame.key_epoch = key_epoch;
    frame.subscription_generation = subscription_generation;
    frame.authorized = authorized;
    frame.subscribed = subscribed;
    frame.length = static_cast<uint16_t>(pet_protocol::encode_control(
        control, crypto, frame.data.data(), frame.data.size()));
    assert(frame.length != 0);
    return frame;
}

codex_pet::PetTransferResult handle(
    Rig& rig,
    const CodexPetControlFrame& frame,
    uint32_t now_ms,
    uint32_t connection_generation = kConnectionGeneration,
    uint32_t key_epoch = kKeyEpoch,
    uint64_t subscription_generation = kSubscriptionGeneration)
{
    return rig.transfer.handle(
        frame,
        now_ms,
        connection_generation,
        key_epoch,
        subscription_generation);
}

codex_pet::PetTransferResult poll(
    Rig& rig,
    uint32_t now_ms,
    uint32_t connection_generation = kConnectionGeneration,
    uint32_t key_epoch = kKeyEpoch,
    uint64_t subscription_generation = kSubscriptionGeneration)
{
    return rig.transfer.poll(
        now_ms,
        connection_generation,
        key_epoch,
        subscription_generation);
}

pet_protocol::Control begin_control(
    const std::vector<uint8_t>& package,
    uint32_t transaction_id,
    const voice_input::crypto::ExchangeNonce& exchange_nonce)
{
    const auto details = metadata(package);
    pet_protocol::Control control{};
    control.kind = pet_protocol::Kind::Begin;
    control.sequence = 1;
    control.exchange_nonce = exchange_nonce;
    control.transaction_id = transaction_id;
    control.schema = details.schema;
    control.width = details.width;
    control.height = details.height;
    control.frame_count = details.frame_count;
    control.package_length = static_cast<uint32_t>(package.size());
    control.sha256 = details.hash;
    return control;
}

pet_protocol::Control transaction_control(
    pet_protocol::Kind kind,
    uint32_t sequence,
    uint32_t transaction_id,
    const voice_input::crypto::ExchangeNonce& exchange_nonce)
{
    pet_protocol::Control control{};
    control.kind = kind;
    control.sequence = sequence;
    control.exchange_nonce = exchange_nonce;
    control.transaction_id = transaction_id;
    return control;
}

pet_protocol::Status decode_status(
    const codex_pet::PetTransferResult& result,
    voice_input::crypto::CcmContext& crypto)
{
    assert(result.has_status && result.status_size != 0);
    pet_protocol::Status status{};
    assert(pet_protocol::decode_status(
        result.status.data(), result.status_size, crypto, status));
    return status;
}

codex_pet::PetTransferResult send_package(
    Rig& rig,
    const std::vector<uint8_t>& package,
    uint32_t transaction_id,
    uint8_t nonce_seed,
    uint32_t now_ms,
    CodexPetControlFrame* commit_output = nullptr)
{
    const auto exchange_nonce = nonce(nonce_seed);
    auto result = handle(rig,
        frame_for(
            begin_control(package, transaction_id, exchange_nonce),
            rig.crypto),
        now_ms,
        kConnectionGeneration,
        kKeyEpoch);
    assert(result.accepted);
    assert(decode_status(result, rig.crypto).kind == pet_protocol::Kind::Progress);

    uint32_t sequence = 2;
    uint32_t offset = 0;
    while (offset < package.size()) {
        const size_t chunk_size = std::min(
            pet_protocol::kMaxChunkBytes,
            package.size() - offset);
        auto chunk = transaction_control(
            pet_protocol::Kind::Chunk,
            sequence++,
            transaction_id,
            exchange_nonce);
        chunk.offset = offset;
        chunk.data_size = chunk_size;
        std::copy_n(
            package.data() + offset, chunk_size, chunk.data.begin());
        result = handle(rig,
            frame_for(chunk, rig.crypto),
            now_ms + sequence,
            kConnectionGeneration,
            kKeyEpoch);
        assert(result.accepted);
        assert(result.progress == offset + chunk_size);
        offset += static_cast<uint32_t>(chunk_size);
    }

    const auto commit_frame = frame_for(
        transaction_control(
            pet_protocol::Kind::Commit,
            sequence,
            transaction_id,
            exchange_nonce),
        rig.crypto);
    if (commit_output != nullptr) {
        *commit_output = commit_frame;
    }
    result = handle(rig,
        commit_frame,
        now_ms + sequence,
        kConnectionGeneration,
        kKeyEpoch);
    return result;
}

void test_authorization_subscription_and_stale_context_fail_closed(
    const std::vector<uint8_t>& package)
{
    Rig rig{};
    const auto begin = begin_control(package, 1, nonce(1));
    assert(!handle(rig,
        frame_for(begin, rig.crypto, kConnectionGeneration, kKeyEpoch, false),
        10,
        kConnectionGeneration,
        kKeyEpoch).accepted);
    assert(!handle(rig,
        frame_for(begin, rig.crypto, kConnectionGeneration, kKeyEpoch, true, false),
        11,
        kConnectionGeneration,
        kKeyEpoch).accepted);
    assert(!handle(rig,
        frame_for(begin, rig.crypto, kConnectionGeneration - 1, kKeyEpoch),
        12,
        kConnectionGeneration,
        kKeyEpoch).accepted);
    assert(!handle(rig,
        frame_for(begin, rig.crypto, kConnectionGeneration, kKeyEpoch - 1),
        13,
        kConnectionGeneration,
        kKeyEpoch).accepted);
    assert(!rig.transfer.transaction_active());
    assert(rig.transfer.selection().source == codex_pet::Source::BuiltIn);
}

void test_duplicate_conflict_replay_and_disconnect_keep_old_selection(
    const std::vector<uint8_t>& package)
{
    Rig rig{};
    const auto installed = send_package(rig, package, 10, 10, 100);
    assert(installed.accepted && installed.selection_changed);
    const auto old_selection = rig.transfer.selection();
    assert(old_selection.source == codex_pet::Source::Custom);
    rig.transfer.record_status_delivery(installed, true);

    const auto exchange_nonce = nonce(20);
    auto result = handle(rig,
        frame_for(begin_control(package, 20, exchange_nonce), rig.crypto),
        200,
        kConnectionGeneration,
        kKeyEpoch);
    assert(result.accepted && rig.transfer.transaction_active());

    auto chunk = transaction_control(
        pet_protocol::Kind::Chunk, 2, 20, exchange_nonce);
    chunk.offset = 0;
    chunk.data_size = std::min(package.size(), pet_protocol::kMaxChunkBytes);
    std::copy_n(package.data(), chunk.data_size, chunk.data.begin());
    const auto encoded_chunk = frame_for(chunk, rig.crypto);
    result = handle(rig,
        encoded_chunk, 201, kConnectionGeneration, kKeyEpoch);
    assert(result.accepted && result.progress == chunk.data_size);
    const auto duplicate = handle(rig,
        encoded_chunk, 202, kConnectionGeneration, kKeyEpoch);
    assert(duplicate.accepted && duplicate.progress == chunk.data_size);

    chunk.data[0] ^= 1U;
    const auto conflict = handle(rig,
        frame_for(chunk, rig.crypto),
        203,
        kConnectionGeneration,
        kKeyEpoch);
    assert(!conflict.accepted);
    assert(decode_status(conflict, rig.crypto).reason ==
           pet_protocol::ErrorReason::Replay);
    assert(!rig.transfer.transaction_active());
    assert(rig.transfer.selection().hash == old_selection.hash);

    const auto replayed_begin = handle(rig,
        frame_for(begin_control(package, 20, exchange_nonce), rig.crypto),
        204,
        kConnectionGeneration,
        kKeyEpoch);
    assert(!replayed_begin.accepted);

    result = handle(rig,
        frame_for(begin_control(package, 21, nonce(21)), rig.crypto),
        210,
        kConnectionGeneration,
        kKeyEpoch);
    assert(result.accepted && rig.transfer.transaction_active());
    rig.transfer.on_disconnect(kConnectionGeneration);
    assert(!rig.transfer.transaction_active());
    assert(rig.transfer.selection().hash == old_selection.hash);
}

void test_commit_once_cancel_timeout_context_retirement_and_restore(
    const std::vector<uint8_t>& package)
{
    Rig rig{};
    CodexPetControlFrame committed_control{};
    auto committed = send_package(
        rig, package, 30, 30, 300, &committed_control);
    assert(committed.accepted && committed.selection_changed);
    assert(decode_status(committed, rig.crypto).kind ==
           pet_protocol::Kind::Committed);
    const auto installed = rig.transfer.selection();
    assert(installed.generation == 1);
    assert(installed.hash == metadata(package).hash);
    rig.transfer.record_status_delivery(committed, true);

    const auto late_commit = handle(rig,
        committed_control,
        400,
        kConnectionGeneration,
        kKeyEpoch);
    assert(!late_commit.accepted);
    assert(rig.transfer.selection().generation == 1);

    auto result = handle(rig,
        frame_for(begin_control(package, 31, nonce(31)), rig.crypto),
        410,
        kConnectionGeneration,
        kKeyEpoch);
    assert(result.accepted);
    result = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Cancel, 2, 31, nonce(31)),
            rig.crypto),
        411,
        kConnectionGeneration,
        kKeyEpoch);
    assert(result.accepted);
    assert(decode_status(result, rig.crypto).kind ==
           pet_protocol::Kind::Cancelled);
    assert(!rig.transfer.transaction_active());

    result = handle(rig,
        frame_for(begin_control(package, 32, nonce(32)), rig.crypto),
        500,
        kConnectionGeneration,
        kKeyEpoch);
    assert(result.accepted);
    const auto timed_out = poll(rig,
        500 + codex_pet::kPetTransferTimeoutMs,
        kConnectionGeneration,
        kKeyEpoch);
    assert(!timed_out.accepted);
    assert(decode_status(timed_out, rig.crypto).reason ==
           pet_protocol::ErrorReason::Timeout);
    assert(!rig.transfer.transaction_active());

    result = handle(rig,
        frame_for(begin_control(package, 33, nonce(33)), rig.crypto),
        600,
        kConnectionGeneration,
        kKeyEpoch);
    assert(result.accepted);
    rig.transfer.on_credential_replaced(kKeyEpoch + 1);
    assert(!rig.transfer.transaction_active());
    assert(rig.transfer.selection().hash == installed.hash);

    result = handle(rig,
        frame_for(begin_control(package, 34, nonce(34)), rig.crypto),
        700,
        kConnectionGeneration,
        kKeyEpoch);
    assert(result.accepted);
    const auto stale_switch = handle(rig,
        frame_for(
            begin_control(package, 35, nonce(35)),
            rig.crypto,
            kConnectionGeneration,
            kKeyEpoch),
        701,
        kConnectionGeneration + 1,
        kKeyEpoch);
    assert(!stale_switch.accepted);
    assert(!rig.transfer.transaction_active());
    assert(rig.transfer.selection().hash == installed.hash);

    const auto query = transaction_control(
        pet_protocol::Kind::Query, 1, 36, nonce(36));
    result = handle(rig,
        frame_for(
            query, rig.crypto, kConnectionGeneration + 1, kKeyEpoch),
        800,
        kConnectionGeneration + 1,
        kKeyEpoch);
    assert(result.accepted);
    auto status = decode_status(result, rig.crypto);
    assert(status.kind == pet_protocol::Kind::State);
    assert(status.source == pet_protocol::Source::Custom);
    assert(status.active_sha256 == installed.hash);

    const auto restore = transaction_control(
        pet_protocol::Kind::Restore, 1, 37, nonce(37));
    result = handle(rig,
        frame_for(
            restore, rig.crypto, kConnectionGeneration + 1, kKeyEpoch),
        900,
        kConnectionGeneration + 1,
        kKeyEpoch);
    assert(result.accepted && result.selection_changed);
    status = decode_status(result, rig.crypto);
    assert(status.kind == pet_protocol::Kind::State);
    assert(status.source == pet_protocol::Source::BuiltIn);
    assert(rig.transfer.selection().source == codex_pet::Source::BuiltIn);
    assert(rig.transfer.selection().generation == 2);
}

void test_cleanup_warning_is_bounded_and_selection_remains_queryable(
    const std::vector<uint8_t>& package)
{
    Rig rig{};
    const auto first = send_package(rig, package, 40, 40, 1000);
    assert(first.accepted);
    rig.transfer.record_status_delivery(first, true);

    const auto second = send_package(rig, package, 41, 41, 1100);
    assert(second.accepted);
    const auto installed = rig.transfer.selection();
    assert(installed.generation == 2);
    rig.files.fail_cleanup = true;
    rig.transfer.record_status_delivery(second, true);
    assert(rig.transfer.selection().hash == installed.hash);

    const auto blocked = handle(rig,
        frame_for(begin_control(package, 42, nonce(42)), rig.crypto),
        1200,
        kConnectionGeneration,
        kKeyEpoch);
    assert(!blocked.accepted);
    assert(decode_status(blocked, rig.crypto).reason ==
           pet_protocol::ErrorReason::Busy);

    const auto query = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Query, 1, 43, nonce(43)),
            rig.crypto),
        1210,
        kConnectionGeneration,
        kKeyEpoch);
    assert(!query.accepted);
    assert(decode_status(query, rig.crypto).reason ==
           pet_protocol::ErrorReason::IoFailure);
    assert(rig.transfer.selection().generation == installed.generation);
    rig.files.fail_cleanup = false;
    rig.transfer.record_status_delivery(query, true);

    const auto recovered = handle(rig,
        frame_for(begin_control(package, 44, nonce(44)), rig.crypto),
        1220,
        kConnectionGeneration,
        kKeyEpoch);
    assert(recovered.accepted);
    rig.transfer.shutdown();
    assert(!rig.transfer.transaction_active());
    assert(rig.transfer.selection().hash == installed.hash);
}

void test_restore_cleanup_warning_surfaces_error_and_query_retries_cleanup(
    const std::vector<uint8_t>& package)
{
    Rig rig{};
    const auto first = send_package(rig, package, 50, 50, 1300);
    assert(first.accepted);
    rig.transfer.record_status_delivery(first, true);
    const auto second = send_package(rig, package, 51, 51, 1400);
    assert(second.accepted);
    assert(rig.files.present[0] && rig.files.present[1]);

    const auto restore = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Restore, 1, 52, nonce(52)),
            rig.crypto),
        1500,
        kConnectionGeneration,
        kKeyEpoch);
    assert(restore.accepted);
    assert(restore.selection_changed);
    assert(decode_status(restore, rig.crypto).kind ==
           pet_protocol::Kind::State);
    assert(rig.transfer.selection().source == codex_pet::Source::BuiltIn);
    assert(rig.files.present[0] && rig.files.present[1]);

    // Notification failure/disconnect cannot cross the cleanup boundary.
    rig.transfer.record_status_delivery(restore, false);
    rig.transfer.record_status_delivery(restore, true);
    assert(rig.files.present[0] && rig.files.present[1]);

    const auto first_query = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Query, 1, 53, nonce(53)),
            rig.crypto),
        1510,
        kConnectionGeneration,
        kKeyEpoch);
    assert(first_query.accepted);
    rig.files.fail_cleanup = true;
    rig.transfer.record_status_delivery(first_query, true);
    assert(rig.files.present[0] && rig.files.present[1]);

    const auto warning_query = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Query, 1, 54, nonce(54)),
            rig.crypto),
        1520,
        kConnectionGeneration,
        kKeyEpoch);
    assert(!warning_query.accepted);
    assert(decode_status(warning_query, rig.crypto).reason ==
           pet_protocol::ErrorReason::IoFailure);
    rig.transfer.record_status_delivery(warning_query, false);
    assert(rig.files.present[0] && rig.files.present[1]);

    rig.files.fail_cleanup = false;
    const auto second_query = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Query, 1, 55, nonce(55)),
            rig.crypto),
        1520,
        kConnectionGeneration,
        kKeyEpoch);
    assert(!second_query.accepted);
    assert(decode_status(second_query, rig.crypto).reason ==
           pet_protocol::ErrorReason::IoFailure);
    rig.transfer.record_status_delivery(second_query, true);
    assert(!rig.files.present[0] && !rig.files.present[1]);

    // Each delivered response may acknowledge cleanup at most once.
    rig.files.fail_cleanup = true;
    rig.transfer.record_status_delivery(second_query, true);
    assert(!rig.files.present[0] && !rig.files.present[1]);

    const auto recovered = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Query, 1, 56, nonce(56)),
            rig.crypto),
        1530,
        kConnectionGeneration,
        kKeyEpoch);
    assert(recovered.accepted);
    assert(decode_status(recovered, rig.crypto).kind ==
           pet_protocol::Kind::State);
}

void test_restore_cleanup_receipt_is_fenced_by_disconnect_and_generation(
    const std::vector<uint8_t>& package)
{
    Rig rig{};
    const auto committed = send_package(rig, package, 57, 57, 1540);
    rig.transfer.record_status_delivery(committed, true);
    assert(rig.files.present[0] || rig.files.present[1]);

    const auto restore = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Restore, 1, 58, nonce(58)),
            rig.crypto),
        1550,
        kConnectionGeneration,
        kKeyEpoch);
    assert(restore.accepted && restore.cleanup_after_delivery);
    rig.transfer.on_disconnect(kConnectionGeneration);
    rig.transfer.record_status_delivery(restore, true);
    assert(rig.files.present[0] || rig.files.present[1]);

    constexpr uint32_t next_connection_generation =
        kConnectionGeneration + 1U;
    const auto query = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Query, 1, 59, nonce(59)),
            rig.crypto,
            next_connection_generation),
        1560,
        next_connection_generation,
        kKeyEpoch);
    assert(query.accepted && query.cleanup_after_delivery);
    auto wrong_generation = query;
    ++wrong_generation.key_epoch;
    rig.transfer.record_status_delivery(wrong_generation, true);
    assert(rig.files.present[0] || rig.files.present[1]);

    const auto retry = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Query, 1, 60, nonce(60)),
            rig.crypto,
            next_connection_generation),
        1570,
        next_connection_generation,
        kKeyEpoch);
    rig.transfer.record_status_delivery(retry, true);
    assert(!rig.files.present[0] && !rig.files.present[1]);
}

void test_busy_response_nonce_is_tombstoned_and_exact_retry_is_cached(
    const std::vector<uint8_t>& package)
{
    Rig rig{};
    const auto completed = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Query, 1, 59, nonce(59)),
            rig.crypto),
        1590,
        kConnectionGeneration,
        kKeyEpoch);
    assert(completed.accepted);
    const auto active_nonce = nonce(60);
    const auto active = handle(rig,
        frame_for(begin_control(package, 60, active_nonce), rig.crypto),
        1600,
        kConnectionGeneration,
        kKeyEpoch);
    assert(active.accepted);

    const auto competitor = frame_for(
        transaction_control(
            pet_protocol::Kind::Query, 1, 61, nonce(61)),
        rig.crypto);
    const auto busy = handle(rig,
        competitor, 1610, kConnectionGeneration, kKeyEpoch);
    assert(!busy.accepted);
    assert(decode_status(busy, rig.crypto).reason ==
           pet_protocol::ErrorReason::Busy);

    const auto active_nonce_collision = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Query, 1, 63, active_nonce),
            rig.crypto),
        1610,
        kConnectionGeneration,
        kKeyEpoch);
    assert(!active_nonce_collision.has_status);

    const auto active_transaction_collision = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Query, 1, 60, nonce(64)),
            rig.crypto),
        1610,
        kConnectionGeneration,
        kKeyEpoch);
    assert(!active_transaction_collision.has_status);

    const auto tombstoned_nonce_collision = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Query, 1, 65, nonce(59)),
            rig.crypto),
        1610,
        kConnectionGeneration,
        kKeyEpoch);
    assert(!tombstoned_nonce_collision.has_status);

    const auto exact_retry = handle(rig,
        competitor, 1611, kConnectionGeneration, kKeyEpoch);
    assert(exact_retry.status_size == busy.status_size);
    assert(std::equal(
        busy.status.begin(),
        busy.status.begin() + busy.status_size,
        exact_retry.status.begin()));

    const auto nonce_collision = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Query, 1, 62, nonce(61)),
            rig.crypto),
        1612,
        kConnectionGeneration,
        kKeyEpoch);
    assert(!nonce_collision.has_status);

    const auto transaction_collision = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Query, 1, 61, nonce(62)),
            rig.crypto),
        1613,
        kConnectionGeneration,
        kKeyEpoch);
    assert(!transaction_collision.has_status);

    const auto cancelled = handle(rig,
        frame_for(
            transaction_control(
                pet_protocol::Kind::Cancel, 2, 60, active_nonce),
            rig.crypto),
        1620,
        kConnectionGeneration,
        kKeyEpoch);
    assert(cancelled.accepted);
    const auto late_exact_retry = handle(rig,
        competitor, 1621, kConnectionGeneration, kKeyEpoch);
    assert(late_exact_retry.status_size == busy.status_size);
    assert(std::equal(
        busy.status.begin(),
        busy.status.begin() + busy.status_size,
        late_exact_retry.status.begin()));
}

void test_control_queued_before_subscription_loss_cannot_start_after_resubscribe(
    const std::vector<uint8_t>& package)
{
    Rig rig{};
    const auto queued_before_loss = frame_for(
        begin_control(package, 70, nonce(70)), rig.crypto);
    rig.transfer.on_subscription_changed(2, false);
    rig.transfer.on_subscription_changed(3, true);
    const auto stale = handle(rig,
        queued_before_loss,
        1700,
        kConnectionGeneration,
        kKeyEpoch,
        3);
    assert(!stale.accepted);
    assert(!stale.has_status);
    assert(!rig.transfer.transaction_active());
}

void test_subscription_changes_preserve_crypto_replay_history(
    const std::vector<uint8_t>& package)
{
    Rig rig{};
    const auto completed_control = transaction_control(
        pet_protocol::Kind::Query, 1, 80, nonce(80));
    const auto completed = handle(rig,
        frame_for(completed_control, rig.crypto),
        1800,
        kConnectionGeneration,
        kKeyEpoch);
    assert(completed.accepted);

    const auto active_control = begin_control(package, 81, nonce(81));
    const auto active = handle(rig,
        frame_for(active_control, rig.crypto),
        1810,
        kConnectionGeneration,
        kKeyEpoch);
    assert(active.accepted);

    CodexPetControlFrame competing = frame_for(
        transaction_control(
            pet_protocol::Kind::Query, 1, 82, nonce(82)),
        rig.crypto);
    const auto busy = handle(rig,
        competing,
        1820,
        kConnectionGeneration,
        kKeyEpoch);
    assert(busy.has_status);
    assert(decode_status(busy, rig.crypto).reason ==
           pet_protocol::ErrorReason::Busy);

    rig.transfer.on_subscription_changed(2, false);
    rig.transfer.on_subscription_changed(3, true);

    const auto retired_active_replay = handle(rig,
        frame_for(
            active_control,
            rig.crypto,
            kConnectionGeneration,
            kKeyEpoch,
            true,
            true,
            3),
        1825,
        kConnectionGeneration,
        kKeyEpoch,
        3);
    assert(!retired_active_replay.accepted);
    assert(!retired_active_replay.has_status);

    const auto completed_replay = handle(rig,
        frame_for(
            completed_control,
            rig.crypto,
            kConnectionGeneration,
            kKeyEpoch,
            true,
            true,
            3),
        1830,
        kConnectionGeneration,
        kKeyEpoch,
        3);
    assert(!completed_replay.accepted);
    assert(!completed_replay.has_status);

    competing.subscription_generation = 3;
    const auto busy_retry = handle(rig,
        competing,
        1840,
        kConnectionGeneration,
        kKeyEpoch,
        3);
    assert(!busy_retry.accepted);
    assert(busy_retry.has_status);
    assert(busy_retry.subscription_generation == 3);
    assert(busy_retry.status_size == busy.status_size);
    assert(std::equal(
        busy.status.begin(),
        busy.status.begin() + busy.status_size,
        busy_retry.status.begin()));
}

}  // namespace

int main()
{
    static_assert(codex_pet::kPetControlQueueDepth == 8);
    static_assert(codex_pet::kPetControlsPerLoop == 4);
    static_assert(codex_pet::kPetLoopBudgetUs == 2000);
    static_assert(codex_pet::kPetTransferTimeoutMs == 30000);

    const auto package = read_hex("tests/fixtures/codex_pet/cxp1_minimal.hex");
    test_authorization_subscription_and_stale_context_fail_closed(package);
    test_duplicate_conflict_replay_and_disconnect_keep_old_selection(package);
    test_commit_once_cancel_timeout_context_retirement_and_restore(package);
    test_cleanup_warning_is_bounded_and_selection_remains_queryable(package);
    test_control_queued_before_subscription_loss_cannot_start_after_resubscribe(
        package);
    test_subscription_changes_preserve_crypto_replay_history(package);
    test_busy_response_nonce_is_tombstoned_and_exact_retry_is_cached(package);
    test_restore_cleanup_warning_surfaces_error_and_query_retries_cleanup(
        package);
    test_restore_cleanup_receipt_is_fenced_by_disconnect_and_generation(
        package);
}
