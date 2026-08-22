#include "codex_runtime_session.h"

#include "ble_voice_probe_controller.h"
#include "codex_ble_transport.h"
#include "input_action_controller.h"
#include "pinned_microphone_controller.h"
#include "voice_input_controller.h"

#include <mutex>

class CodexRuntimeSession::Cleanup {
public:
    Cleanup(
        CodexRuntimeSession& session,
        PinnedMicrophoneController& pinned_microphone,
        std::mutex& controller_mutex,
        uint32_t now_ms)
        : _session(session),
          _pinned_microphone(pinned_microphone),
          _controller_mutex(controller_mutex),
          _now_ms(now_ms)
    {
    }

    void disconnect_input_actions()
    {
        if (_session._input_actions != nullptr) {
            _session._input_actions->disconnect();
        }
    }

    void disconnect_voice()
    {
        std::lock_guard<std::mutex> lock(_controller_mutex);
        if (_session._voice_input != nullptr) {
            _pinned_microphone.stop_for_safety(
                *_session._voice_input, _now_ms);
            _session._voice_input->handle_disconnect();
        }
    }

    void disconnect_voice_probe()
    {
        if (_session._voice_probe != nullptr) {
            _session._voice_probe->handle_disconnect();
        }
    }

    void shutdown_voice()
    {
        std::lock_guard<std::mutex> lock(_controller_mutex);
        if (_session._voice_input != nullptr) {
            _pinned_microphone.stop_for_safety(
                *_session._voice_input, _now_ms);
            (void)_session._voice_input->set_task_route(0);
            _session._voice_input->shutdown();
        }
    }

    void shutdown_voice_probe()
    {
        if (_session._voice_probe != nullptr) {
            _session._voice_probe->shutdown();
        }
    }

    void stop_transport()
    {
        if (_session._transport != nullptr) {
            _session._transport->stop();
        }
    }

    void release_resources()
    {
        _session.release_resources();
    }

private:
    CodexRuntimeSession& _session;
    PinnedMicrophoneController& _pinned_microphone;
    std::mutex& _controller_mutex;
    uint32_t _now_ms;
};

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
    Cleanup cleanup(*this, pinned_microphone, controller_mutex, now_ms);
    (void)_lifecycle.disconnect(cleanup);
}

void CodexRuntimeSession::shutdown(
    PinnedMicrophoneController& pinned_microphone,
    std::mutex& controller_mutex,
    uint32_t now_ms)
{
    Cleanup cleanup(*this, pinned_microphone, controller_mutex, now_ms);
    (void)_lifecycle.shutdown(cleanup);
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
