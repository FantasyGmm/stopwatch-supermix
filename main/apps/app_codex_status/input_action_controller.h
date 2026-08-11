#pragma once

#include "codex_button_gesture.h"
#include "input_action_protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

class InputActionReadyReplayGuard {
public:
    bool decode_once(
        const uint8_t* bytes,
        size_t length,
        voice_input::crypto::CcmContext& crypto,
        input_action::ReadyControl& ready);
    void reset();

private:
    static constexpr size_t kMaxControlsPerConnection = 8;
    std::array<
        voice_input::crypto::ExchangeNonce,
        kMaxControlsPerConnection> _nonces = {};
    size_t _count = 0;
};

class InputActionController {
public:
#ifndef STOPWATCH_HOST_TEST
    InputActionController();
#endif
    explicit InputActionController(voice_input::crypto::CcmContext& crypto);

    void set_ready(const input_action::ReadyControl& ready);
    void clear_pending();
    void disconnect();
    bool enqueue(CodexButtonAction action, uint32_t now_ms);
    bool take(input_action::WireAction& output);
    void record_result(bool accepted);

private:
    static constexpr size_t kQueueCapacity = 4;

    voice_input::crypto::CcmContext* _crypto = nullptr;
    voice_input::crypto::ExchangeNonce _connection_nonce = {};
    std::deque<input_action::WireAction> _outgoing;
    uint32_t _next_sequence = 1;
    bool _ready = false;
};
