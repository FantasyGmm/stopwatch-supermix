#pragma once

#include "codex_ble_framing.h"
#include "ble_voice_probe_protocol.h"
#include "agent_connection_router.h"
#include "voice_input_controller.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>


struct CodexBleVoiceControl {
    uint16_t length = 0;
    std::array<uint8_t, ble_voice_probe::kControlCommandSize> data = {};
};

struct CodexProductionVoiceControl {
    voice_input::VoiceControl control = {};
};

struct CodexPairingFrame {
    uint16_t connection_handle = secure_pairing::kNoAgentConnection;
    uint16_t length = 0;
    std::array<uint8_t, 256> data = {};
};

enum class CodexBleNotifyResult { Accepted, NotReady, Congested, InvalidSize };


class CodexBleTransport {
public:
    bool start(const std::string& device_name);
    void stop();
    void poll(uint32_t now_ms);
    bool connected() const;
    uint32_t connection_epoch() const;
    bool take_message(CodexBleMessage& message);
    bool take_voice_control(CodexBleVoiceControl& control);
    bool take_production_voice_control(CodexProductionVoiceControl& control);
    bool take_pairing_frame(CodexPairingFrame& frame);
    bool voice_subscribed() const;
    uint16_t negotiated_mtu() const;
    size_t voice_notification_limit() const;
    CodexBleNotifyResult notify_voice(const uint8_t* data, size_t length);
    CodexBleNotifyResult notify_pairing(
        uint16_t connection_handle,
        const uint8_t* data,
        size_t length);
    bool set_voice_credential(
        const voice_input::crypto::Key& key,
        uint32_t key_epoch);
    void clear_voice_credential();

    bool authorize_gatt(
        uint16_t connection_handle,
        secure_pairing::AgentGattSurface surface) const;
    bool authorize_pending(
        uint16_t connection_handle,
        const secure_pairing::AgentFingerprint& fingerprint);
    bool promote_pending(uint16_t connection_handle);
    bool reject_pending(uint16_t connection_handle);
    uint16_t pending_handle() const;

    bool enqueue_frame_from_ble(
        uint16_t connection_handle,
        const uint8_t* data,
        size_t length);
    bool enqueue_voice_control_from_ble(
        uint16_t connection_handle,
        const uint8_t* data,
        size_t length);
    bool enqueue_pairing_frame_from_ble(
        uint16_t connection_handle,
        const uint8_t* data,
        size_t length);
    bool handle_gap_connected(uint16_t connection_handle);
    void handle_gap_disconnected(uint16_t connection_handle);
    void handle_subscription(
        uint16_t connection_handle,
        uint16_t attr_handle,
        bool enabled);
    void handle_mtu_update(uint16_t connection_handle, uint16_t mtu);
    bool stopping() const;

private:
    void* _frame_queue = nullptr;
    void* _voice_control_queue = nullptr;
    void* _production_voice_control_queue = nullptr;
    void* _pairing_frame_queue = nullptr;
    voice_input::ControlReplayGuard _production_control_guard;
    voice_input::crypto::CcmContext _production_voice_crypto;
    std::atomic<uint32_t> _production_voice_key_epoch{0};
    secure_pairing::AgentConnectionRouter _connection_router;
    CodexBleReassembler _reassembler;
    std::deque<CodexBleMessage> _messages;
    std::atomic<bool> _stopping{false};
    bool _initialized = false;
};
