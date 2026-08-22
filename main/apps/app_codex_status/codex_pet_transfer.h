#pragma once

#include "codex_pet_protocol.h"
#include "codex_pet_store.h"
#include "agent_connection_router.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct CodexPetControlFrame {
    uint16_t connection_handle = secure_pairing::kNoAgentConnection;
    uint16_t length = 0;
    uint32_t connection_generation = 0;
    uint32_t key_epoch = 0;
    bool authorized = false;
    bool subscribed = false;
    std::array<uint8_t, pet_protocol::kMaxFrameBytes> data{};
};

namespace codex_pet {

constexpr size_t kPetControlQueueDepth = 8;
constexpr size_t kPetControlsPerLoop = 4;
constexpr uint32_t kPetLoopBudgetUs = 2000;
constexpr uint32_t kPetTransferTimeoutMs = 30000;

struct PetTransferResult {
    bool accepted = false;
    bool has_status = false;
    bool selection_changed = false;
    bool cleanup_after_delivery = false;
    uint32_t progress = 0;
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
        uint32_t key_epoch);
    PetTransferResult poll(
        uint32_t now_ms,
        uint32_t active_generation,
        uint32_t key_epoch);
    void record_status_delivery(
        const PetTransferResult& result,
        bool delivered);
    void on_disconnect(uint32_t connection_generation);
    void on_credential_replaced(uint32_t key_epoch);
    void on_subscription_lost();
    void shutdown();

    bool transaction_active() const;
    PetSelection selection() const;

private:
    bool sync_context(uint32_t active_generation, uint32_t key_epoch);
    void reset_replay_context();
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
    PetTransferResult make_ephemeral_error(
        const pet_protocol::Control& control,
        pet_protocol::ErrorReason reason);
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
    uint32_t _replay_context_generation = 0;
    uint32_t _transaction_id = 0;
    voice_input::crypto::ExchangeNonce _exchange_nonce{};
    uint32_t _status_sequence = 0;
    uint32_t _last_activity_ms = 0;
    uint32_t _package_length = 0;
    uint32_t _acknowledged_bytes = 0;
};

}  // namespace codex_pet
