#include "codex_pet_transfer.h"

#include <algorithm>
#include <limits>

namespace codex_pet {
namespace {

constexpr uint16_t kSchemaMin = 1;
constexpr uint16_t kSchemaMax = 1;

uint32_t bounded_u32(uint64_t value)
{
    return value > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(value);
}

bool starts_operation(pet_protocol::Kind kind)
{
    return kind == pet_protocol::Kind::Query ||
           kind == pet_protocol::Kind::Begin ||
           kind == pet_protocol::Kind::Restore;
}

pet_protocol::ErrorReason map_store_error(StoreResult result)
{
    switch (result) {
        case StoreResult::Busy:
            return pet_protocol::ErrorReason::Busy;
        case StoreResult::InvalidMetadata:
            return pet_protocol::ErrorReason::InvalidMetadata;
        case StoreResult::UnsupportedSchema:
            return pet_protocol::ErrorReason::UnsupportedSchema;
        case StoreResult::TooLarge:
            return pet_protocol::ErrorReason::TooLarge;
        case StoreResult::InsufficientStorage:
            return pet_protocol::ErrorReason::InsufficientStorage;
        case StoreResult::InvalidOffset:
            return pet_protocol::ErrorReason::InvalidOffset;
        case StoreResult::Incomplete:
            return pet_protocol::ErrorReason::InvalidMetadata;
        case StoreResult::DigestMismatch:
            return pet_protocol::ErrorReason::HashMismatch;
        case StoreResult::InvalidPackage:
            return pet_protocol::ErrorReason::UnusablePackage;
        case StoreResult::IoError:
        case StoreResult::SelectionIoError:
        case StoreResult::CleanupWarning:
            return pet_protocol::ErrorReason::IoFailure;
        case StoreResult::AllocationFailed:
        case StoreResult::GenerationExhausted:
            return pet_protocol::ErrorReason::Internal;
        case StoreResult::Ok:
            return pet_protocol::ErrorReason::Internal;
    }
    return pet_protocol::ErrorReason::Internal;
}

}  // namespace

CodexPetTransfer::CodexPetTransfer(
    CodexPetStore& store,
    voice_input::crypto::CcmContext& crypto)
    : _store(store), _crypto(crypto), _selection(_store.load_selection())
{
}

bool CodexPetTransfer::sync_context(
    uint32_t active_generation,
    uint32_t key_epoch,
    uint64_t subscription_generation)
{
    if (active_generation == 0 || key_epoch == 0) {
        retire_transaction(true);
        clear_competing_responses();
        _context_ready = false;
        _active_generation = 0;
        _key_epoch = 0;
        return false;
    }
    if (!_context_ready || _active_generation != active_generation ||
        _key_epoch != key_epoch) {
        retire_transaction(true);
        clear_competing_responses();
        _context_ready = true;
        _active_generation = active_generation;
        _key_epoch = key_epoch;
        reset_replay_context();
    }
    if (subscription_generation == 0 || !_subscription_active) {
        retire_transaction(true);
        return false;
    }
    if (_subscription_generation == 0) {
        _subscription_generation = subscription_generation;
    }
    return _subscription_generation == subscription_generation;
}

void CodexPetTransfer::reset_replay_context()
{
    ++_replay_context_generation;
    if (_replay_context_generation == 0) {
        ++_replay_context_generation;
    }
    _replay.set_connection_generation(_replay_context_generation);
}

void CodexPetTransfer::clear_competing_responses()
{
    _competing_response_count = 0;
    _competing_responses = {};
}

void CodexPetTransfer::retire_transaction(bool tombstone)
{
    if (_transaction_active) {
        _store.cancel();
    }
    _replay.retire(tombstone);
    _transaction_active = false;
    _transaction_id = 0;
    _exchange_nonce = {};
    _status_sequence = 0;
    _last_activity_ms = 0;
    _package_length = 0;
    _acknowledged_bytes = 0;
}

PetTransferResult CodexPetTransfer::handle(
    const CodexPetControlFrame& frame,
    uint32_t now_ms,
    uint32_t active_generation,
    uint32_t key_epoch,
    uint64_t subscription_generation)
{
    if (!sync_context(
            active_generation, key_epoch, subscription_generation) ||
        !frame.authorized || !frame.subscribed ||
        frame.connection_handle == secure_pairing::kNoAgentConnection ||
        frame.connection_generation != active_generation ||
        frame.key_epoch != key_epoch ||
        frame.subscription_generation != subscription_generation ||
        frame.length == 0 ||
        frame.length > frame.data.size()) {
        return {};
    }

    pet_protocol::Control control{};
    if (!pet_protocol::decode_control(
            frame.data.data(), frame.length, _crypto, control)) {
        return {};
    }

    for (size_t index = 0; index < _competing_response_count; ++index) {
        const CompetingResponse& cached = _competing_responses[index];
        const bool nonce_matches =
            cached.exchange_nonce == control.exchange_nonce;
        const bool transaction_matches =
            cached.transaction_id == control.transaction_id;
        if (!nonce_matches && !transaction_matches) {
            continue;
        }
        if (nonce_matches && transaction_matches &&
            cached.request_size == frame.length &&
            std::equal(
                cached.request.begin(),
                cached.request.begin() + cached.request_size,
                frame.data.begin())) {
            PetTransferResult response = cached.response;
            response.subscription_generation = _subscription_generation;
            return response;
        }
        return {};
    }

    if (!_transaction_active) {
        return handle_new_operation(control, frame, now_ms);
    }
    if (control.transaction_id != _transaction_id ||
        control.exchange_nonce != _exchange_nonce) {
        if (_replay.conflicts(
                control.exchange_nonce, control.transaction_id)) {
            return {};
        }
        if (control.sequence == 1 && starts_operation(control.kind)) {
            return handle_competing_operation(control, frame);
        }
        return {};
    }

    const pet_protocol::ReplayDecision replay = _replay.accept(
        control, frame.data.data(), frame.length);
    if (replay == pet_protocol::ReplayDecision::Rejected) {
        return fail_transaction(pet_protocol::ErrorReason::Replay);
    }
    _last_activity_ms = now_ms;
    return handle_active_operation(control, frame, replay, now_ms);
}

PetTransferResult CodexPetTransfer::handle_new_operation(
    const pet_protocol::Control& control,
    const CodexPetControlFrame& frame,
    uint32_t now_ms)
{
    if (control.sequence != 1 || !starts_operation(control.kind) ||
        !_replay.begin(control.exchange_nonce, control.transaction_id)) {
        return {};
    }
    _transaction_active = true;
    _transaction_id = control.transaction_id;
    _exchange_nonce = control.exchange_nonce;
    _status_sequence = 0;
    _last_activity_ms = now_ms;
    _package_length = 0;
    _acknowledged_bytes = 0;

    const auto replay = _replay.accept(
        control, frame.data.data(), frame.length);
    if (replay != pet_protocol::ReplayDecision::Accepted) {
        return fail_transaction(pet_protocol::ErrorReason::Replay);
    }

    if (control.kind == pet_protocol::Kind::Query) {
        PetTransferResult result{};
        if (_cleanup_warning_pending && _store.cleanup_pending()) {
            pet_protocol::Status status{};
            status.kind = pet_protocol::Kind::Error;
            status.reason = pet_protocol::ErrorReason::IoFailure;
            result = encode_status(status, false, false, true);
        } else {
            result = make_state(false);
        }
        retire_transaction(true);
        return result;
    }
    if (control.kind == pet_protocol::Kind::Restore) {
        const StoreResult restored = _store.restore_default();
        if (restored != StoreResult::Ok) {
            return fail_transaction(map_store_error(restored));
        }
        _selection = _store.load_selection();
        PetTransferResult result = make_state(true);
        retire_transaction(true);
        return result;
    }

    PetMetadata metadata{};
    metadata.schema = control.schema;
    metadata.width = control.width;
    metadata.height = control.height;
    metadata.frame_count = control.frame_count;
    metadata.hash = control.sha256;
    const StoreResult begun = _store.begin(control.package_length, metadata);
    if (begun != StoreResult::Ok) {
        if (begun == StoreResult::InsufficientStorage) {
            return fail_transaction(
                pet_protocol::ErrorReason::InsufficientStorage,
                bounded_u32(_store.required_bytes()),
                bounded_u32(_store.available_bytes()));
        }
        if (begun == StoreResult::TooLarge) {
            return fail_transaction(
                pet_protocol::ErrorReason::TooLarge,
                control.package_length,
                static_cast<uint32_t>(kMaxPackageBytes));
        }
        return fail_transaction(map_store_error(begun));
    }
    _package_length = control.package_length;
    return make_progress(true);
}

PetTransferResult CodexPetTransfer::handle_active_operation(
    const pet_protocol::Control& control,
    const CodexPetControlFrame&,
    pet_protocol::ReplayDecision replay,
    uint32_t)
{
    if (replay == pet_protocol::ReplayDecision::Duplicate) {
        return make_progress(true);
    }
    if (control.kind == pet_protocol::Kind::Chunk) {
        if (control.offset > _package_length ||
            control.data_size > _package_length - control.offset) {
            return fail_transaction(
                pet_protocol::ErrorReason::InvalidOffset);
        }
        const StoreResult written = _store.write(
            control.offset,
            {control.data.data(), control.data_size});
        if (written != StoreResult::Ok) {
            return fail_transaction(map_store_error(written));
        }
        _acknowledged_bytes =
            control.offset + static_cast<uint32_t>(control.data_size);
        return make_progress(true);
    }
    if (control.kind == pet_protocol::Kind::Cancel) {
        _store.cancel();
        PetTransferResult result = make_cancelled();
        _transaction_active = false;
        _replay.retire(true);
        _transaction_id = 0;
        _exchange_nonce = {};
        return result;
    }
    if (control.kind != pet_protocol::Kind::Commit) {
        return fail_transaction(pet_protocol::ErrorReason::InvalidMessage);
    }
    if (_acknowledged_bytes != _package_length) {
        return fail_transaction(pet_protocol::ErrorReason::InvalidMetadata);
    }
    const StoreResult committed = _store.commit();
    if (committed != StoreResult::Ok) {
        return fail_transaction(map_store_error(committed));
    }
    _selection = _store.load_selection();
    PetTransferResult result = make_committed();
    _transaction_active = false;
    _replay.retire(true);
    _transaction_id = 0;
    _exchange_nonce = {};
    return result;
}

PetTransferResult CodexPetTransfer::poll(
    uint32_t now_ms,
    uint32_t active_generation,
    uint32_t key_epoch,
    uint64_t subscription_generation)
{
    if (!sync_context(
            active_generation, key_epoch, subscription_generation) ||
        !_transaction_active ||
        static_cast<uint32_t>(now_ms - _last_activity_ms) <
            kPetTransferTimeoutMs) {
        return {};
    }
    return fail_transaction(pet_protocol::ErrorReason::Timeout);
}

void CodexPetTransfer::record_status_delivery(
    const PetTransferResult& result,
    bool delivered)
{
    if (!result.has_status || !result.cleanup_after_delivery ||
        result.delivery_id == 0 ||
        result.delivery_id <= _last_recorded_cleanup_delivery_id) {
        return;
    }
    _last_recorded_cleanup_delivery_id = result.delivery_id;
    if (!delivered || !_context_ready ||
        result.connection_generation != _active_generation ||
        result.key_epoch != _key_epoch ||
        result.subscription_generation != _subscription_generation ||
        result.selection.generation != _selection.generation) {
        return;
    }
    const StoreResult cleanup = _store.acknowledge();
    if (cleanup == StoreResult::Ok) {
        _cleanup_warning_pending = false;
    } else if (cleanup == StoreResult::CleanupWarning) {
        _cleanup_warning_pending = true;
    }
}

void CodexPetTransfer::on_disconnect(uint32_t connection_generation)
{
    if (!_context_ready || connection_generation == _active_generation) {
        retire_transaction(true);
        clear_competing_responses();
        _context_ready = false;
        _active_generation = 0;
        _key_epoch = 0;
    }
}

void CodexPetTransfer::on_credential_replaced(uint32_t key_epoch)
{
    retire_transaction(true);
    clear_competing_responses();
    _key_epoch = key_epoch;
    if (_context_ready) {
        reset_replay_context();
    }
}

void CodexPetTransfer::on_subscription_changed(
    uint64_t subscription_generation,
    bool subscribed)
{
    if (_subscription_generation != subscription_generation ||
        _subscription_active != subscribed) {
        retire_transaction(true);
    }
    _subscription_generation = subscription_generation;
    _subscription_active = subscribed;
}

void CodexPetTransfer::shutdown()
{
    retire_transaction(true);
    clear_competing_responses();
    _context_ready = false;
    _active_generation = 0;
    _key_epoch = 0;
    _subscription_generation = 0;
    _subscription_active = false;
}

bool CodexPetTransfer::transaction_active() const
{
    return _transaction_active;
}

PetSelection CodexPetTransfer::selection() const
{
    return _selection;
}

PetTransferResult CodexPetTransfer::make_state(bool changed)
{
    pet_protocol::Status status{};
    status.kind = pet_protocol::Kind::State;
    status.source = _selection.source == Source::Custom
        ? pet_protocol::Source::Custom
        : pet_protocol::Source::BuiltIn;
    status.usable = _selection.usable;
    status.schema_min = kSchemaMin;
    status.schema_max = kSchemaMax;
    status.generation = _selection.generation;
    status.active_sha256 = _selection.source == Source::Custom
        ? _selection.hash
        : std::array<uint8_t, 32>{};
    return encode_status(
        status, true, changed, _store.cleanup_pending());
}

PetTransferResult CodexPetTransfer::make_progress(bool accepted)
{
    pet_protocol::Status status{};
    status.kind = pet_protocol::Kind::Progress;
    status.acknowledged_bytes = _acknowledged_bytes;
    status.total_bytes = _package_length;
    PetTransferResult result = encode_status(status, accepted);
    result.progress = _acknowledged_bytes;
    return result;
}

PetTransferResult CodexPetTransfer::make_committed()
{
    pet_protocol::Status status{};
    status.kind = pet_protocol::Kind::Committed;
    status.generation = _selection.generation;
    status.active_sha256 = _selection.hash;
    return encode_status(
        status, true, true, _store.cleanup_pending());
}

PetTransferResult CodexPetTransfer::make_cancelled()
{
    pet_protocol::Status status{};
    status.kind = pet_protocol::Kind::Cancelled;
    return encode_status(status, true);
}

PetTransferResult CodexPetTransfer::make_error(
    pet_protocol::ErrorReason reason,
    uint32_t required_bytes,
    uint32_t available_bytes)
{
    pet_protocol::Status status{};
    status.kind = pet_protocol::Kind::Error;
    status.reason = reason;
    status.required_bytes = required_bytes;
    status.available_bytes = available_bytes;
    return encode_status(status, false);
}

PetTransferResult CodexPetTransfer::handle_competing_operation(
    const pet_protocol::Control& control,
    const CodexPetControlFrame& frame)
{
    if (_competing_response_count >= _competing_responses.size()) {
        return {};
    }
    pet_protocol::Status status{};
    status.kind = pet_protocol::Kind::Error;
    status.sequence = 1;
    status.exchange_nonce = control.exchange_nonce;
    status.transaction_id = control.transaction_id;
    status.reason = pet_protocol::ErrorReason::Busy;
    PetTransferResult result{};
    result.status_size = pet_protocol::encode_status(
        status, _crypto, result.status.data(), result.status.size());
    result.has_status = result.status_size != 0;
    result.subscription_generation = _subscription_generation;
    if (!result.has_status) {
        return {};
    }
    CompetingResponse& cached =
        _competing_responses[_competing_response_count++];
    cached.transaction_id = control.transaction_id;
    cached.exchange_nonce = control.exchange_nonce;
    cached.request_size = frame.length;
    std::copy_n(frame.data.begin(), frame.length, cached.request.begin());
    cached.response = result;
    return result;
}

PetTransferResult CodexPetTransfer::encode_status(
    pet_protocol::Status status,
    bool accepted,
    bool selection_changed,
    bool cleanup_after_delivery)
{
    PetTransferResult result{};
    if (!_transaction_active || _status_sequence == UINT32_MAX) {
        return result;
    }
    status.sequence = ++_status_sequence;
    status.exchange_nonce = _exchange_nonce;
    status.transaction_id = _transaction_id;
    result.status_size = pet_protocol::encode_status(
        status, _crypto, result.status.data(), result.status.size());
    if (result.status_size == 0) {
        return result;
    }
    result.accepted = accepted;
    result.has_status = true;
    result.selection_changed = selection_changed;
    result.cleanup_after_delivery = cleanup_after_delivery;
    result.selection = _selection;
    result.connection_generation = _active_generation;
    result.key_epoch = _key_epoch;
    result.subscription_generation = _subscription_generation;
    ++_next_delivery_id;
    if (_next_delivery_id == 0) {
        ++_next_delivery_id;
    }
    result.delivery_id = _next_delivery_id;
    return result;
}

PetTransferResult CodexPetTransfer::fail_transaction(
    pet_protocol::ErrorReason reason,
    uint32_t required_bytes,
    uint32_t available_bytes)
{
    PetTransferResult result = make_error(
        reason, required_bytes, available_bytes);
    retire_transaction(true);
    return result;
}

}  // namespace codex_pet
