#pragma once

#include "codex_pet_protocol.h"
#include "codex_pet_store.h"
#include "agent_connection_router.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

struct CodexPetControlFrame {
    uint16_t connection_handle = secure_pairing::kNoAgentConnection;
    uint16_t length = 0;
    uint32_t connection_generation = 0;
    uint32_t key_epoch = 0;
    uint64_t subscription_generation = 0;
    bool authorized = false;
    bool subscribed = false;
    std::array<uint8_t, pet_protocol::kMaxFrameBytes> data{};
};

namespace codex_pet {

constexpr size_t kPetControlQueueDepth = 8;
constexpr size_t kPetControlsPerLoop = 4;
constexpr uint32_t kPetLoopBudgetUs = 2000;
constexpr uint32_t kPetTransferTimeoutMs = 30000;

struct PetSubscriptionSnapshot {
    uint64_t generation = 0;
    bool subscribed = false;
};

struct PetSubscriptionTransition {
    PetSubscriptionSnapshot snapshot{};
    bool changed = false;
    bool lost = false;
};

class PetSubscriptionTracker {
public:
    PetSubscriptionTransition update(bool subscribed)
    {
        uint32_t current = _state.load();
        while (true) {
            const PetSubscriptionSnapshot before = decode(current);
            if (before.subscribed == subscribed) {
                return {before, false, false};
            }
            const uint64_t next_generation =
                before.generation == kMaxGeneration
                ? kMaxGeneration
                : before.generation + 1U;
            const bool next_subscribed =
                before.generation == kMaxGeneration ? false : subscribed;
            const uint32_t next = encode(
                {next_generation, next_subscribed});
            if (_state.compare_exchange_weak(current, next)) {
                return {
                    {next_generation, next_subscribed},
                    true,
                    before.subscribed && !next_subscribed,
                };
            }
        }
    }

    PetSubscriptionSnapshot snapshot() const
    {
        return decode(_state.load());
    }

    bool accepts(uint64_t generation) const
    {
        const PetSubscriptionSnapshot current = snapshot();
        return current.subscribed && generation != 0 &&
               generation == current.generation;
    }

    void reset()
    {
        _state = 0;
    }

private:
    static constexpr uint64_t kMaxGeneration =
        std::numeric_limits<uint32_t>::max() >> 1U;

    static uint32_t encode(PetSubscriptionSnapshot snapshot)
    {
        return static_cast<uint32_t>(snapshot.generation << 1U) |
               (snapshot.subscribed ? 1U : 0U);
    }

    static PetSubscriptionSnapshot decode(uint32_t state)
    {
        return {state >> 1U, (state & 1U) != 0};
    }

    std::atomic<uint32_t> _state{0};
};

class PetCleanupSequence {
public:
    template <typename PetCleanup, typename RuntimeCleanup>
    static decltype(auto) run(
        PetCleanup&& pet_cleanup,
        RuntimeCleanup&& runtime_cleanup)
    {
        std::forward<PetCleanup>(pet_cleanup)();
        return std::forward<RuntimeCleanup>(runtime_cleanup)();
    }
};

struct PetTransferResult {
    bool accepted = false;
    bool has_status = false;
    bool selection_changed = false;
    bool cleanup_after_delivery = false;
    uint32_t progress = 0;
    uint64_t subscription_generation = 0;
    PetSelection selection{};
    size_t status_size = 0;
    std::array<uint8_t, pet_protocol::kMaxFrameBytes> status{};
};

class CodexPetTransfer {
public:
    CodexPetTransfer(
        CodexPetStore& store,
        voice_input::crypto::CcmContext& crypto);

    PetTransferResult handle(
        const CodexPetControlFrame& frame,
        uint32_t now_ms,
        uint32_t active_generation,
        uint32_t key_epoch,
        uint64_t subscription_generation);
    PetTransferResult poll(
        uint32_t now_ms,
        uint32_t active_generation,
        uint32_t key_epoch,
        uint64_t subscription_generation);
    void record_status_delivery(
        const PetTransferResult& result,
        bool delivered);
    void on_disconnect(uint32_t connection_generation);
    void on_credential_replaced(uint32_t key_epoch);
    void on_subscription_changed(
        uint64_t subscription_generation,
        bool subscribed);
    void shutdown();

    bool transaction_active() const;
    PetSelection selection() const;

private:
    struct CompetingResponse {
        uint32_t transaction_id = 0;
        voice_input::crypto::ExchangeNonce exchange_nonce{};
        size_t request_size = 0;
        std::array<uint8_t, pet_protocol::kMaxFrameBytes> request{};
        PetTransferResult response{};
    };

    bool sync_context(
        uint32_t active_generation,
        uint32_t key_epoch,
        uint64_t subscription_generation);
    void reset_replay_context();
    void clear_competing_responses();
    void retire_transaction(bool tombstone);
    PetTransferResult handle_new_operation(
        const pet_protocol::Control& control,
        const CodexPetControlFrame& frame,
        uint32_t now_ms);
    PetTransferResult handle_active_operation(
        const pet_protocol::Control& control,
        const CodexPetControlFrame& frame,
        pet_protocol::ReplayDecision replay,
        uint32_t now_ms);
    PetTransferResult make_state(bool changed);
    PetTransferResult make_progress(bool accepted);
    PetTransferResult make_committed();
    PetTransferResult make_cancelled();
    PetTransferResult make_error(
        pet_protocol::ErrorReason reason,
        uint32_t required_bytes = 0,
        uint32_t available_bytes = 0);
    PetTransferResult handle_competing_operation(
        const pet_protocol::Control& control,
        const CodexPetControlFrame& frame);
    PetTransferResult encode_status(
        pet_protocol::Status status,
        bool accepted,
        bool selection_changed = false,
        bool cleanup_after_delivery = false);
    PetTransferResult fail_transaction(
        pet_protocol::ErrorReason reason,
        uint32_t required_bytes = 0,
        uint32_t available_bytes = 0);

    CodexPetStore& _store;
    voice_input::crypto::CcmContext& _crypto;
    pet_protocol::ControlReplayGuard _replay;
    PetSelection _selection{};
    bool _context_ready = false;
    bool _transaction_active = false;
    uint32_t _active_generation = 0;
    uint32_t _key_epoch = 0;
    uint64_t _subscription_generation = 0;
    bool _subscription_active = true;
    uint32_t _replay_context_generation = 0;
    uint32_t _transaction_id = 0;
    voice_input::crypto::ExchangeNonce _exchange_nonce{};
    uint32_t _status_sequence = 0;
    uint32_t _last_activity_ms = 0;
    uint32_t _package_length = 0;
    uint32_t _acknowledged_bytes = 0;
    size_t _competing_response_count = 0;
    std::array<
        CompetingResponse,
        pet_protocol::kCompletedTransactionTombstones>
        _competing_responses{};
};

}  // namespace codex_pet
