#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

class BleVoiceProbeController;
class CodexBleTransport;
class InputActionController;
class PinnedMicrophoneController;
class VoiceInputController;

enum class CodexRuntimeCleanupAction : uint8_t {
    None,
    Disconnect,
    Shutdown,
};

class CodexRuntimeSessionLifecycle {
public:
    void begin()
    {
        _state = State::Active;
    }

    template <typename Cleanup>
    CodexRuntimeCleanupAction disconnect(Cleanup& cleanup)
    {
        if (_state != State::Active) {
            return CodexRuntimeCleanupAction::None;
        }
        _state = State::Disconnected;
        cleanup.disconnect_input_actions();
        cleanup.disconnect_voice();
        cleanup.disconnect_voice_probe();
        return CodexRuntimeCleanupAction::Disconnect;
    }

    template <typename Cleanup>
    CodexRuntimeCleanupAction shutdown(Cleanup& cleanup)
    {
        if (_state == State::Shutdown) {
            return CodexRuntimeCleanupAction::None;
        }
        _state = State::Shutdown;
        cleanup.disconnect_input_actions();
        cleanup.shutdown_voice();
        cleanup.shutdown_voice_probe();
        cleanup.stop_transport();
        cleanup.release_resources();
        return CodexRuntimeCleanupAction::Shutdown;
    }

private:
    enum class State : uint8_t {
        Shutdown,
        Active,
        Disconnected,
    };

    State _state = State::Shutdown;
};

class CodexRuntimeSession {
public:
    CodexRuntimeSession() = default;
    ~CodexRuntimeSession();

    CodexRuntimeSession(const CodexRuntimeSession&) = delete;
    CodexRuntimeSession& operator=(const CodexRuntimeSession&) = delete;

    bool start(const std::string& device_name);
    void begin_connection();
    void handle_disconnect(
        PinnedMicrophoneController& pinned_microphone,
        std::mutex& controller_mutex,
        uint32_t now_ms);
    void shutdown(
        PinnedMicrophoneController& pinned_microphone,
        std::mutex& controller_mutex,
        uint32_t now_ms);

    CodexBleTransport* transport() const;
    InputActionController* input_actions() const;
    BleVoiceProbeController* voice_probe() const;
    VoiceInputController* voice_input() const;

private:
    class Cleanup;

    void release_resources();

    CodexRuntimeSessionLifecycle _lifecycle;
    std::unique_ptr<CodexBleTransport> _transport;
    std::unique_ptr<InputActionController> _input_actions;
    std::unique_ptr<BleVoiceProbeController> _voice_probe;
    std::unique_ptr<VoiceInputController> _voice_input;
};
