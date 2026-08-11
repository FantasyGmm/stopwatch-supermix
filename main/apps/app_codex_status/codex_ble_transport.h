#pragma once

#include "codex_ble_framing.h"
#include "ble_voice_probe_protocol.h"
#include "input_action_controller.h"
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

struct CodexInputActionControl {
    input_action::ReadyControl control = {};
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
    bool take_input_action_control(CodexInputActionControl& control);
    bool voice_subscribed() const;
    bool input_action_subscribed() const;
    uint16_t negotiated_mtu() const;
    size_t voice_notification_limit() const;
    CodexBleNotifyResult notify_voice(const uint8_t* data, size_t length);
    CodexBleNotifyResult notify_input_action(const uint8_t* data, size_t length);

    bool enqueue_frame_from_ble(const uint8_t* data, size_t length);
    bool enqueue_voice_control_from_ble(const uint8_t* data, size_t length);
    bool enqueue_input_control_from_ble(const uint8_t* data, size_t length);
    void handle_gap_connected(uint16_t connection_handle);
    void handle_gap_disconnected();
    void handle_voice_subscription(uint16_t attr_handle, bool enabled);
    void handle_input_subscription(uint16_t attr_handle, bool enabled);
    void handle_mtu_update(uint16_t connection_handle, uint16_t mtu);
    bool stopping() const;

private:
    void* _frame_queue = nullptr;
    void* _voice_control_queue = nullptr;
    void* _production_voice_control_queue = nullptr;
    void* _input_control_queue = nullptr;
    voice_input::ControlReplayGuard _production_control_guard;
    InputActionReadyReplayGuard _input_control_guard;
    CodexBleReassembler _reassembler;
    std::deque<CodexBleMessage> _messages;
    std::atomic<bool> _connected{false};
    std::atomic<bool> _stopping{false};
    std::atomic<bool> _voice_subscribed{false};
    std::atomic<bool> _input_action_subscribed{false};
    std::atomic<uint16_t> _negotiated_mtu{23};
    bool _initialized = false;
    std::atomic<uint16_t> _connection_handle{0xffff};
    std::atomic<uint16_t> _input_tx_value_handle{0};
    std::atomic<uint32_t> _connection_epoch{0};
};
