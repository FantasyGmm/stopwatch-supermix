#include "codex_runtime_session.h"

#include "ble_voice_probe_controller.h"
#include "codex_ble_transport.h"
#include "input_action_controller.h"
#include "pinned_microphone_controller.h"
#include "voice_input_controller.h"

#include <mutex>

CodexRuntimeSession::~CodexRuntimeSession()
{
    if (_voice_input != nullptr) {
        _voice_input->shutdown();
    }
    if (_voice_probe != nullptr) {
        _voice_probe->shutdown();
    }
    if (_transport != nullptr) {
        _transport->stop();
    }
    release_resources();
}

bool CodexRuntimeSession::start(const std::string& device_name)
{
    if (_transport != nullptr) {
        return false;
    }

    _transport = std::make_unique<CodexBleTransport>();
    if (!_transport->start(device_name)) {
        _transport.reset();
        return false;
    }

    _input_actions = std::make_unique<InputActionController>(
        _transport->input_action_crypto());
    _voice_probe = std::make_unique<BleVoiceProbeController>();
    if (!_voice_probe->initialize()) {
        _voice_probe.reset();
    }
    _voice_input = std::make_unique<VoiceInputController>();
    _voice_input->set_notification_transport(_transport.get());
    if (!_voice_input->initialize()) {
        _voice_input.reset();
    }

    _lifecycle.begin();
    return true;
}

void CodexRuntimeSession::begin_connection()
{
    if (_transport != nullptr) {
        _lifecycle.begin();
    }
}

void CodexRuntimeSession::handle_disconnect(
    PinnedMicrophoneController& pinned_microphone,
    std::mutex& controller_mutex,
    uint32_t now_ms)
{
    if (_lifecycle.disconnect() !=
        CodexRuntimeCleanupAction::Disconnect) {
        return;
    }
    if (_input_actions != nullptr) {
        _input_actions->disconnect();
    }
    {
        std::lock_guard<std::mutex> lock(controller_mutex);
        if (_voice_input != nullptr) {
            pinned_microphone.stop_for_safety(*_voice_input, now_ms);
            _voice_input->handle_disconnect();
        }
    }
    if (_voice_probe != nullptr) {
        _voice_probe->handle_disconnect();
    }
}

void CodexRuntimeSession::shutdown(
    PinnedMicrophoneController& pinned_microphone,
    std::mutex& controller_mutex,
    uint32_t now_ms)
{
    if (_lifecycle.shutdown() != CodexRuntimeCleanupAction::Shutdown) {
        return;
    }
    if (_input_actions != nullptr) {
        _input_actions->disconnect();
    }
    {
        std::lock_guard<std::mutex> lock(controller_mutex);
        if (_voice_input != nullptr) {
            pinned_microphone.stop_for_safety(*_voice_input, now_ms);
            (void)_voice_input->set_task_route(0);
            _voice_input->shutdown();
        }
    }
    if (_voice_probe != nullptr) {
        _voice_probe->shutdown();
    }
    if (_transport != nullptr) {
        _transport->stop();
    }
    release_resources();
}

CodexBleTransport* CodexRuntimeSession::transport() const
{
    return _transport.get();
}

InputActionController* CodexRuntimeSession::input_actions() const
{
    return _input_actions.get();
}

BleVoiceProbeController* CodexRuntimeSession::voice_probe() const
{
    return _voice_probe.get();
}

VoiceInputController* CodexRuntimeSession::voice_input() const
{
    return _voice_input.get();
}

void CodexRuntimeSession::release_resources()
{
    _voice_input.reset();
    _voice_probe.reset();
    _input_actions.reset();
    _transport.reset();
}
