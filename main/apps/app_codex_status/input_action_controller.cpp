#include "input_action_controller.h"

#include <algorithm>

bool InputActionReadyReplayGuard::decode_once(
    const uint8_t* bytes,
    size_t length,
    voice_input::crypto::CcmContext& crypto,
    input_action::ReadyControl& ready)
{
    input_action::ReadyControl candidate = {};
    if (_count >= _nonces.size() ||
        !input_action::decode_ready(bytes, length, crypto, candidate)) {
        return false;
    }
    const auto end = _nonces.begin() + _count;
    if (std::find(_nonces.begin(), end, candidate.connection_nonce) != end) {
        return false;
    }
    _nonces[_count++] = candidate.connection_nonce;
    ready = candidate;
    return true;
}

void InputActionReadyReplayGuard::reset()
{
    _nonces = {};
    _count = 0;
}

#ifndef STOPWATCH_HOST_TEST
InputActionController::InputActionController()
    : InputActionController(voice_input::crypto::device_context())
{
}
#endif

InputActionController::InputActionController(
    voice_input::crypto::CcmContext& crypto)
    : _crypto(&crypto)
{
}

void InputActionController::set_ready(
    const input_action::ReadyControl& ready)
{
    disconnect();
    const bool nonce_is_nonzero = std::any_of(
        ready.connection_nonce.begin(),
        ready.connection_nonce.end(),
        [](uint8_t value) { return value != 0; });
    if (_crypto == nullptr || !_crypto->ready() || !nonce_is_nonzero) {
        return;
    }
    _connection_nonce = ready.connection_nonce;
    _ready = true;
}

void InputActionController::disconnect()
{
    _connection_nonce = {};
    _outgoing.clear();
    _next_sequence = 1;
    _ready = false;
}

void InputActionController::clear_pending()
{
    _outgoing.clear();
}

bool InputActionController::enqueue(CodexButtonAction action, uint32_t now_ms)
{
    if (!_ready || _crypto == nullptr || _next_sequence == 0 ||
        _outgoing.size() >= kQueueCapacity ||
        action == CodexButtonAction::None) {
        return false;
    }

    input_action::Kind kind = input_action::Kind::Submit;
    if (action == CodexButtonAction::ForceSteer) {
        kind = input_action::Kind::ForceSteer;
    } else if (action != CodexButtonAction::Submit) {
        return false;
    }

    input_action::WireAction wire = {};
    wire.logical = {kind, _next_sequence, now_ms};
    const size_t encoded = input_action::encode_action(
        wire.logical,
        _connection_nonce,
        *_crypto,
        wire.bytes.data(),
        wire.bytes.size());
    if (encoded != wire.bytes.size()) {
        return false;
    }
    _outgoing.push_back(wire);
    ++_next_sequence;
    return true;
}

bool InputActionController::take(input_action::WireAction& output)
{
    if (_outgoing.empty()) {
        return false;
    }
    output = _outgoing.front();
    _outgoing.pop_front();
    return true;
}

void InputActionController::record_result(bool accepted)
{
    // Notifications are at-most-once: take() permanently dequeues the action.
    (void)accepted;
}
