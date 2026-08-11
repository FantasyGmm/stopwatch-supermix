#pragma once

#include "codex_button_gesture.h"
#include "voice_capture_pipeline.h"
#include "voice_input_protocol.h"
#include "voice_input_session.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

namespace voice_input {

struct Notification {
    NotificationKind kind = NotificationKind::VoiceError;
    uint16_t session = 0;
    uint32_t sequence = 0;
    size_t size = 0;
    std::array<uint8_t, kMaxNotificationSize> bytes = {};
};

class ControlReplayGuard {
public:
    bool decode_once(
        const uint8_t* bytes,
        size_t length,
        crypto::CcmContext& crypto,
        VoiceControl& control);
    void reset();

private:
    static constexpr size_t kMaxControlsPerConnection = 32;
    std::array<crypto::ExchangeNonce, kMaxControlsPerConnection> _nonces = {};
    size_t _count = 0;
};

}  // namespace voice_input

class VoiceInputEntropy {
public:
    virtual ~VoiceInputEntropy() = default;
    virtual uint32_t random32() = 0;
};

enum class VoiceInputUiState : uint8_t {
    Off,
    Ready,
    Active,
    Error,
};

enum class VoiceButtonAction : uint8_t {
    None,
    ShortClick,
    Press,
    Release,
    CancelVoice,
    CancelHome,
};

enum class VoiceNotificationSendResult : uint8_t {
    Accepted,
    Congested,
    PermanentFailure,
};

class VoiceButtonEdgeGate {
public:
    // Keeps PTT start latency below 100 ms while reserving A+B for GoHome.
    static constexpr uint32_t kChordGuardMs = 90;

    void reset(bool right_pressed);
    VoiceButtonAction update(
        bool left_pressed,
        bool right_pressed,
        bool woke_screen,
        bool go_home,
        uint32_t now_ms);

private:
    enum class State : uint8_t {
        Idle,
        Pending,
        Active,
        SuppressedUntilRightRelease,
        SuppressedUntilBothRelease,
    };

    State _state = State::Idle;
    uint32_t _pending_since_ms = 0;
};

class VoiceButtonSink {
public:
    virtual ~VoiceButtonSink() = default;
    virtual void handle_button(bool pressed, uint32_t now_ms) = 0;
    virtual void cancel_for_home(uint32_t now_ms) = 0;
};

class CodexButtonActionSink {
public:
    virtual ~CodexButtonActionSink() = default;
    virtual void handle_action(CodexButtonAction action, uint32_t now_ms) = 0;
};

enum class VoiceButtonDispatchDecision : uint8_t {
    Continue,
    Close,
};

class VoiceButtonDispatcher {
public:
    void reset(bool right_pressed);
    VoiceButtonDispatchDecision dispatch(
        bool left_pressed,
        bool right_pressed,
        bool woke_screen,
        bool go_home,
        uint32_t now_ms,
        VoiceButtonSink* sink,
        CodexButtonActionSink* action_sink = nullptr);

private:
    VoiceButtonEdgeGate _gate;
    CodexButtonGesture _gesture;
};

class VoiceInputController : public VoiceButtonSink {
public:
#ifndef STOPWATCH_HOST_TEST
    VoiceInputController();
#endif
    VoiceInputController(
        VoiceCaptureBackend& backend,
        voice_input::crypto::CcmContext& crypto,
        VoiceInputEntropy& entropy);
    ~VoiceInputController();

    bool initialize();
    void shutdown();
    void set_ready(bool ready, uint16_t negotiated_mtu);
    void handle_connection_epoch(uint32_t connection_epoch);
    void handle_button(bool pressed, uint32_t now_ms) override;
    void cancel_for_home(uint32_t now_ms) override;
    void handle_disconnect();
    void poll(uint32_t now_ms);
    bool take_notification(voice_input::Notification& output);
    void record_notification_result(
        const voice_input::Notification& notification,
        VoiceNotificationSendResult result,
        uint32_t now_ms);
    VoiceInputUiState ui_state() const;

#if defined(STOPWATCH_HOST_TEST)
    size_t queued_audio_ms_for_test() const;
    void set_next_connection_session_for_test(uint16_t session);
#endif

private:
    static void producer_entry(void* context);
    void producer_loop();
    void producer_step(uint32_t now_ms);
    bool start_session(uint32_t now_ms);
    bool prepare_begin(uint32_t now_ms);
    bool prepare_audio(
        const std::array<uint8_t, VoiceCapturePipeline::kAdpcmBytesPerBlock>& adpcm,
        uint32_t captured_samples,
        uint32_t now_ms);
    bool prepare_terminal(
        voice_input::NotificationKind kind,
        const uint8_t* payload,
        size_t payload_size,
        uint32_t now_ms);
    bool prepare_terminal_at(
        voice_input::NotificationKind kind,
        const uint8_t* payload,
        size_t payload_size,
        uint32_t sequence,
        uint32_t now_ms);
    void finalize_graceful_stop(uint32_t now_ms);
    void abort_with_failure(uint16_t code, uint16_t detail, uint32_t now_ms);
    void fail(uint16_t code, uint16_t detail, uint32_t now_ms);
    void stop_capture_bounded();
    void reset_session(bool ready_after_reset, bool clear_notifications);
    bool make_session_identity(
        uint16_t& session,
        voice_input::crypto::ExchangeNonce& nonce);
    bool enqueue_encoded(
        const voice_input::VoiceNotification& logical,
        voice_input::Notification& wire);
    void wake_producer();

    std::unique_ptr<VoiceCapturePipeline> _capture_pipeline;
    voice_input::crypto::CcmContext* _crypto = nullptr;
    VoiceInputEntropy* _entropy = nullptr;
    std::unique_ptr<VoiceInputEntropy> _owned_entropy;
    voice_input::VoiceInputSession _session;
    voice_input::crypto::ExchangeNonce _session_nonce = {};
    voice_input::crypto::ExchangeNonce _last_session_nonce = {};
    std::deque<voice_input::Notification> _outgoing;
    mutable std::mutex _mutex;
    std::atomic<bool> _producer_running{false};
    std::atomic<bool> _producer_step_active{false};
    std::atomic<bool> _external_failure_requested{false};
    std::atomic<bool> _shutdown_requested{false};
    void* _producer_task = nullptr;
    [[maybe_unused]] void* _producer_stopped = nullptr;
    uint16_t _negotiated_mtu = 23;
    uint16_t _next_session = 1;
    uint32_t _connection_epoch = 0;
    uint32_t _last_accepted_sequence = 0;
    uint8_t _congestion_retries = 0;
    size_t _queued_audio_packets = 0;
    bool _button_pressed = false;
    bool _begin_accepted = false;
    bool _has_accepted_notification = false;
    std::atomic<bool> _ready{false};
    bool _graceful_stop = false;
    bool _terminal_pending = false;
    bool _initialized = false;
    VoiceInputUiState _ui_state = VoiceInputUiState::Off;
};
