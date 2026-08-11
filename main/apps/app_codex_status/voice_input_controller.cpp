#include "voice_input_controller.h"

#if !defined(STOPWATCH_HOST_TEST)
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

#include <algorithm>
#if defined(STOPWATCH_HOST_TEST)
#include <thread>
#endif

namespace {

[[maybe_unused]] constexpr size_t kProducerStackBytes = 8192;
constexpr uint32_t kMaxBufferedAudioMs = 300;
constexpr uint32_t kAudioBlockMs = 20;
constexpr size_t kMaxQueuedAudioPackets = kMaxBufferedAudioMs / kAudioBlockMs;
constexpr uint8_t kMaxCongestionRetries = 3;
constexpr uint16_t kMinimumAudioMtu =
    static_cast<uint16_t>(voice_input::kAudioCharacteristicValueSize + 3);
[[maybe_unused]] constexpr uint32_t kProducerStopTimeoutMs = 500;
[[maybe_unused]] constexpr uint32_t kProducerCancelGraceMs = 250;
static_assert(kProducerStopTimeoutMs > VoiceCapturePipeline::kCaptureReadTimeoutMs);
static_assert(kProducerCancelGraceMs > VoiceCapturePipeline::kCaptureReadTimeoutMs);

enum class VoiceFailure : uint16_t {
    CaptureBusy = 1,
    RecorderOpen = 2,
    Resampler = 3,
    Encoder = 4,
    QueueOverflow = 5,
    Crypto = 6,
    SequenceExhausted = 7,
    InvalidState = 8,
};

uint16_t failure_code(VoiceCaptureError error)
{
    switch (error) {
        case VoiceCaptureError::CaptureBusy:
            return static_cast<uint16_t>(VoiceFailure::CaptureBusy);
        case VoiceCaptureError::RecorderOpenFailed:
            return static_cast<uint16_t>(VoiceFailure::RecorderOpen);
        case VoiceCaptureError::ResamplerFailed:
            return static_cast<uint16_t>(VoiceFailure::Resampler);
        case VoiceCaptureError::EncoderFailed:
            return static_cast<uint16_t>(VoiceFailure::Encoder);
        case VoiceCaptureError::None:
            break;
    }
    return static_cast<uint16_t>(VoiceFailure::Encoder);
}

#if !defined(STOPWATCH_HOST_TEST)
class DeviceEntropy final : public VoiceInputEntropy {
public:
    uint32_t random32() override
    {
        return esp_random();
    }
};

uint32_t monotonic_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}
#endif

}  // namespace

namespace voice_input {

bool ControlReplayGuard::decode_once(
    const uint8_t* bytes,
    size_t length,
    crypto::CcmContext& crypto,
    VoiceControl& control)
{
    VoiceControl candidate = {};
    if (!decode_control(bytes, length, crypto, candidate) || _count >= _nonces.size()) {
        return false;
    }
    if (std::find(_nonces.begin(), _nonces.begin() + _count,
                  candidate.exchange_nonce) != _nonces.begin() + _count) {
        return false;
    }
    _nonces[_count++] = candidate.exchange_nonce;
    control = candidate;
    return true;
}

void ControlReplayGuard::reset()
{
    _nonces = {};
    _count = 0;
}

}  // namespace voice_input

void VoiceButtonEdgeGate::reset(bool right_pressed)
{
    _state = right_pressed
        ? State::SuppressedUntilRightRelease
        : State::Idle;
    _pending_since_ms = 0;
}

VoiceButtonAction VoiceButtonEdgeGate::update(
    bool left_pressed,
    bool right_pressed,
    bool woke_screen,
    bool go_home,
    uint32_t now_ms)
{
    if (woke_screen) {
        if (left_pressed && right_pressed) {
            _state = State::SuppressedUntilBothRelease;
        } else if (right_pressed) {
            _state = State::SuppressedUntilRightRelease;
        } else {
            _state = State::Idle;
        }
        return VoiceButtonAction::None;
    }
    if (go_home) {
        _state = State::SuppressedUntilBothRelease;
        return VoiceButtonAction::CancelHome;
    }

    switch (_state) {
    case State::Idle:
        if (!right_pressed) {
            return VoiceButtonAction::None;
        }
        if (left_pressed) {
            _state = State::SuppressedUntilBothRelease;
            return VoiceButtonAction::None;
        }
        _state = State::Pending;
        _pending_since_ms = now_ms;
        return VoiceButtonAction::None;

    case State::Pending:
        if (left_pressed) {
            _state = State::SuppressedUntilBothRelease;
            return VoiceButtonAction::None;
        }
        if (!right_pressed) {
            _state = State::Idle;
            return VoiceButtonAction::ShortClick;
        }
        if (now_ms - _pending_since_ms < kChordGuardMs) {
            return VoiceButtonAction::None;
        }
        _state = State::Active;
        return VoiceButtonAction::Press;

    case State::Active:
        if (left_pressed) {
            _state = State::SuppressedUntilBothRelease;
            return VoiceButtonAction::CancelVoice;
        }
        if (!right_pressed) {
            _state = State::Idle;
            return VoiceButtonAction::Release;
        }
        return VoiceButtonAction::None;

    case State::SuppressedUntilRightRelease:
        if (!right_pressed) {
            _state = State::Idle;
        }
        return VoiceButtonAction::None;

    case State::SuppressedUntilBothRelease:
        if (!left_pressed && !right_pressed) {
            _state = State::Idle;
        }
        return VoiceButtonAction::None;
    }

    return VoiceButtonAction::None;
}

bool VoiceButtonEdgeGate::is_suppressed() const
{
    return _state == State::SuppressedUntilRightRelease ||
           _state == State::SuppressedUntilBothRelease;
}

void VoiceButtonDispatcher::reset(bool right_pressed)
{
    _gate.reset(right_pressed);
    _gesture.reset();
}

VoiceButtonDispatchDecision VoiceButtonDispatcher::dispatch(
    bool left_pressed,
    bool right_pressed,
    bool woke_screen,
    bool go_home,
    uint32_t now_ms,
    VoiceButtonSink* sink,
    CodexButtonActionSink* action_sink,
    bool input_suppressed)
{
    if (input_suppressed) {
        _gate.reset(right_pressed);
    }
    const VoiceButtonAction action = _gate.update(
        left_pressed, right_pressed, woke_screen, go_home, now_ms);
    const CodexButtonAction codex_action = _gesture.update(
        action == VoiceButtonAction::ShortClick,
        right_pressed,
        action == VoiceButtonAction::Press,
        now_ms,
        input_suppressed || _gate.is_suppressed() || woke_screen || go_home);
    if (action_sink != nullptr && codex_action != CodexButtonAction::None) {
        action_sink->handle_action(codex_action, now_ms);
    }
    switch (action) {
    case VoiceButtonAction::ShortClick:
        break;
    case VoiceButtonAction::Press:
        if (sink) {
            sink->handle_button(true, now_ms);
        }
        break;
    case VoiceButtonAction::Release:
        if (sink) {
            sink->handle_button(false, now_ms);
        }
        break;
    case VoiceButtonAction::CancelVoice:
        if (sink) {
            sink->cancel_for_home(now_ms);
        }
        break;
    case VoiceButtonAction::CancelHome:
        if (sink) {
            sink->cancel_for_home(now_ms);
        }
        return VoiceButtonDispatchDecision::Close;
    case VoiceButtonAction::None:
        break;
    }
    return VoiceButtonDispatchDecision::Continue;
}

#if !defined(STOPWATCH_HOST_TEST)
VoiceInputController::VoiceInputController()
    : _capture_pipeline(std::make_unique<VoiceCapturePipeline>()),
      _crypto(&voice_input::crypto::device_context()),
      _owned_entropy(std::make_unique<DeviceEntropy>())
{
    _entropy = _owned_entropy.get();
}
#endif

VoiceInputController::VoiceInputController(
    VoiceCaptureBackend& backend,
    voice_input::crypto::CcmContext& crypto,
    VoiceInputEntropy& entropy)
    : _capture_pipeline(std::make_unique<VoiceCapturePipeline>(backend)),
      _crypto(&crypto),
      _entropy(&entropy)
{
}

VoiceInputController::~VoiceInputController()
{
    shutdown();
}

bool VoiceInputController::initialize()
{
    if (_initialized) {
        return true;
    }
    if (_capture_pipeline == nullptr || _crypto == nullptr || !_crypto->ready() ||
        _entropy == nullptr) {
        _ui_state = VoiceInputUiState::Error;
        return false;
    }
#if !defined(STOPWATCH_HOST_TEST)
    _shutdown_requested = false;
    _producer_stopped = xSemaphoreCreateBinary();
    if (_producer_stopped == nullptr ||
        xTaskCreate(
            producer_entry,
            "codex_voice",
            kProducerStackBytes,
            this,
            5,
            reinterpret_cast<TaskHandle_t*>(&_producer_task)) != pdPASS) {
        if (_producer_stopped != nullptr) {
            vSemaphoreDelete(static_cast<SemaphoreHandle_t>(_producer_stopped));
            _producer_stopped = nullptr;
        }
        _producer_task = nullptr;
        _ui_state = VoiceInputUiState::Error;
        return false;
    }
#else
    _shutdown_requested = false;
#endif
    _initialized = true;
    return true;
}

void VoiceInputController::shutdown()
{
    if (!_initialized && _producer_task == nullptr) {
        if (_capture_pipeline != nullptr) {
            _capture_pipeline->close();
        }
        return;
    }
    _shutdown_requested = true;
    _ready = false;
    _capture_pipeline->requestStop();
    wake_producer();
#if !defined(STOPWATCH_HOST_TEST)
    if (_producer_task != nullptr) {
        TaskHandle_t task = static_cast<TaskHandle_t>(_producer_task);
        bool stopped = _producer_stopped != nullptr &&
                       xSemaphoreTake(
                           static_cast<SemaphoreHandle_t>(_producer_stopped),
                           pdMS_TO_TICKS(kProducerStopTimeoutMs)) == pdTRUE;
        if (!stopped) {
            xTaskAbortDelay(task);
            xTaskNotifyGive(task);
            stopped = _producer_stopped != nullptr &&
                      xSemaphoreTake(
                          static_cast<SemaphoreHandle_t>(_producer_stopped),
                          pdMS_TO_TICKS(kProducerCancelGraceMs)) == pdTRUE;
        }
        (void)stopped;
        vTaskDelete(task);
        _producer_task = nullptr;
    }
    if (_producer_stopped != nullptr) {
        vSemaphoreDelete(static_cast<SemaphoreHandle_t>(_producer_stopped));
        _producer_stopped = nullptr;
    }
#endif
    _producer_running = false;
    _capture_pipeline->close();
    reset_session(false, true);
    _initialized = false;
}

void VoiceInputController::set_ready(bool ready, uint16_t negotiated_mtu)
{
    const bool usable = ready && negotiated_mtu >= kMinimumAudioMtu;
    _negotiated_mtu = negotiated_mtu;
    if (!usable) {
        handle_disconnect();
        return;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    _ready = true;
    if (_session.state() == voice_input::VoiceInputState::Unavailable) {
        _session.set_ready(true);
    }
    if (!_producer_running.load() && !_terminal_pending) {
        _ui_state = VoiceInputUiState::Ready;
    }
}

void VoiceInputController::handle_connection_epoch(uint32_t connection_epoch)
{
    if (connection_epoch == _connection_epoch) {
        return;
    }
    handle_disconnect();
    std::lock_guard<std::mutex> lock(_mutex);
    _connection_epoch = connection_epoch;
    _next_session = 1;
    _last_session_nonce = {};
}

void VoiceInputController::handle_button(bool pressed, uint32_t now_ms)
{
    if (pressed == _button_pressed) {
        return;
    }
    _button_pressed = pressed;
    if (pressed) {
        if (!_ready.load() || _negotiated_mtu < kMinimumAudioMtu) {
            return;
        }
        start_session(now_ms);
        return;
    }

    bool should_stop = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_ui_state == VoiceInputUiState::Error &&
            (_session.state() == voice_input::VoiceInputState::Ready ||
             _session.state() == voice_input::VoiceInputState::Unavailable)) {
            _ui_state = _ready.load() ? VoiceInputUiState::Ready
                                      : VoiceInputUiState::Off;
        }
        should_stop = _session.release(now_ms);
        if (should_stop) {
            _graceful_stop = true;
        }
    }
    if (should_stop) {
        _capture_pipeline->requestStop();
        wake_producer();
    }
}

void VoiceInputController::cancel_for_home(uint32_t now_ms)
{
    (void)now_ms;
    _capture_pipeline->requestStop();
    wake_producer();
    stop_capture_bounded();
    _capture_pipeline->close();
    std::lock_guard<std::mutex> lock(_mutex);
    _session.cancel_for_home();
    _outgoing.clear();
    _queued_audio_packets = 0;
    _graceful_stop = false;
    _terminal_pending = false;
    _ui_state = _ready.load() ? VoiceInputUiState::Ready
                              : VoiceInputUiState::Off;
}

void VoiceInputController::handle_disconnect()
{
    _ready = false;
    _button_pressed = false;
    _capture_pipeline->requestStop();
    wake_producer();
    stop_capture_bounded();
    _capture_pipeline->close();
    reset_session(false, true);
}

void VoiceInputController::poll(uint32_t now_ms)
{
#if defined(STOPWATCH_HOST_TEST)
    if (_producer_running.load()) {
        producer_step(now_ms);
    }
#endif
    finalize_graceful_stop(now_ms);
}

bool VoiceInputController::take_notification(voice_input::Notification& output)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_outgoing.empty()) {
        return false;
    }
    output = _outgoing.front();
    return true;
}

void VoiceInputController::record_notification_result(
    const voice_input::Notification& notification,
    VoiceNotificationSendResult result,
    uint32_t now_ms)
{
    bool notification_failed = false;
    bool terminal_delivery_failed = false;
    voice_input::Notification failed = {};
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_outgoing.empty() ||
            _outgoing.front().kind != notification.kind ||
            _outgoing.front().session != notification.session ||
            _outgoing.front().sequence != notification.sequence) {
            return;
        }
        if (result == VoiceNotificationSendResult::Congested &&
            ++_congestion_retries <= kMaxCongestionRetries) {
            return;
        }
        if (result == VoiceNotificationSendResult::Congested) {
            result = VoiceNotificationSendResult::PermanentFailure;
        }

        if (result == VoiceNotificationSendResult::Accepted) {
            failed = _outgoing.front();
            _outgoing.pop_front();
            _congestion_retries = 0;
            if (failed.kind == voice_input::NotificationKind::Audio &&
                _queued_audio_packets != 0) {
                --_queued_audio_packets;
            }
            _last_accepted_sequence = failed.sequence;
            _has_accepted_notification = true;
            if (failed.kind == voice_input::NotificationKind::PttBegin) {
                _begin_accepted = true;
            } else if (failed.kind == voice_input::NotificationKind::Audio) {
                _session.record_accepted();
            } else if (failed.kind == voice_input::NotificationKind::PttEnd) {
                _session.stopped();
                _terminal_pending = false;
                _begin_accepted = false;
                _ui_state = _ready.load() ? VoiceInputUiState::Ready
                                          : VoiceInputUiState::Off;
            } else if (failed.kind == voice_input::NotificationKind::VoiceError) {
                _terminal_pending = false;
            }
        } else {
            failed = _outgoing.front();
            _outgoing.pop_front();
            _congestion_retries = 0;
            _session.record_queue_overflow();
            terminal_delivery_failed =
                failed.kind == voice_input::NotificationKind::VoiceError;
            notification_failed = !terminal_delivery_failed;
            if (terminal_delivery_failed) {
                _outgoing.clear();
                _queued_audio_packets = 0;
                _terminal_pending = false;
                _ready = false;
                _session.disconnect();
                _ui_state = VoiceInputUiState::Error;
            }
        }
    }
    if (notification_failed) {
        abort_with_failure(
            static_cast<uint16_t>(VoiceFailure::QueueOverflow),
            static_cast<uint16_t>(failed.kind),
            now_ms);
    }
}

VoiceInputUiState VoiceInputController::ui_state() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _ui_state;
}

#if defined(STOPWATCH_HOST_TEST)
size_t VoiceInputController::queued_audio_ms_for_test() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _queued_audio_packets * kAudioBlockMs;
}

void VoiceInputController::set_next_connection_session_for_test(uint16_t session)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _next_session = session;
}
#endif

void VoiceInputController::producer_entry(void* context)
{
#if !defined(STOPWATCH_HOST_TEST)
    static_cast<VoiceInputController*>(context)->producer_loop();
#else
    (void)context;
#endif
}

void VoiceInputController::producer_loop()
{
#if !defined(STOPWATCH_HOST_TEST)
    while (!_shutdown_requested.load()) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (_producer_running.load() && !_shutdown_requested.load()) {
            producer_step(monotonic_ms());
        }
    }
    if (_producer_stopped != nullptr) {
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(_producer_stopped));
    }
    vTaskSuspend(nullptr);
#endif
}

void VoiceInputController::producer_step(uint32_t now_ms)
{
    bool expected = false;
    if (!_producer_step_active.compare_exchange_strong(expected, true)) {
        return;
    }
    struct ActiveGuard {
        std::atomic<bool>& active;
        std::atomic<bool>& running;
        bool completed = false;
        ~ActiveGuard()
        {
            active = false;
            if (completed) {
                running = false;
            }
        }
    } active_guard{_producer_step_active, _producer_running};

    std::array<uint8_t, VoiceCapturePipeline::kAdpcmBytesPerBlock> adpcm = {};
    uint32_t captured_samples = 0;
    VoiceCaptureError error = VoiceCaptureError::None;
    const VoiceCaptureResult result = _capture_pipeline->captureBlock(
        adpcm, captured_samples, error);
    if (result == VoiceCaptureResult::BlockReady) {
        if (!prepare_audio(adpcm, captured_samples, now_ms)) {
            _capture_pipeline->requestStop();
            _capture_pipeline->close();
            active_guard.completed = true;
            return;
        }
        return;
    }
    if (result == VoiceCaptureResult::Continue) {
        return;
    }

    _capture_pipeline->close();
    if (result == VoiceCaptureResult::Failed &&
        !_external_failure_requested.load()) {
        fail(failure_code(error), 0, now_ms);
    }
    active_guard.completed = true;
}

bool VoiceInputController::start_session(uint32_t now_ms)
{
    if (!initialize()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_ready.load() || !_session.press(now_ms)) {
            return false;
        }
        _outgoing.clear();
        _queued_audio_packets = 0;
        _graceful_stop = false;
        _terminal_pending = false;
        _begin_accepted = false;
        _has_accepted_notification = false;
        _last_accepted_sequence = 0;
        _congestion_retries = 0;
    }

    VoiceCaptureError capture_error = VoiceCaptureError::None;
    if (!_capture_pipeline->open(capture_error)) {
        fail(failure_code(capture_error), 0, now_ms);
        return false;
    }

    uint16_t session = 0;
    voice_input::crypto::ExchangeNonce nonce = {};
    if (!make_session_identity(session, nonce)) {
        _capture_pipeline->close();
        fail(static_cast<uint16_t>(VoiceFailure::Crypto), 0, now_ms);
        return false;
    }
    bool session_started = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _session_nonce = nonce;
        session_started = _session.started(session, now_ms, _negotiated_mtu);
    }
    if (!session_started) {
        _capture_pipeline->close();
        fail(static_cast<uint16_t>(VoiceFailure::InvalidState), 0, now_ms);
        return false;
    }
    if (!prepare_begin(now_ms)) {
        _capture_pipeline->close();
        fail(static_cast<uint16_t>(VoiceFailure::Crypto), 0, now_ms);
        return false;
    }
    _producer_running = true;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _ui_state = VoiceInputUiState::Active;
    }
    wake_producer();
    return true;
}

bool VoiceInputController::prepare_begin(uint32_t now_ms)
{
    std::lock_guard<std::mutex> lock(_mutex);
    voice_input::VoiceNotification logical = {};
    logical.kind = voice_input::NotificationKind::PttBegin;
    logical.session = _session.session();
    logical.sequence = 0;
    logical.watch_ms = now_ms;
    logical.session_nonce = _session_nonce;
    logical.payload_size = voice_input::encode_begin_body(
        voice_input::BeginBody{}, logical.payload.data(), logical.payload.size());
    voice_input::Notification wire = {};
    if (logical.payload_size == 0 || !enqueue_encoded(logical, wire)) {
        return false;
    }
    _outgoing.push_back(wire);
    return true;
}

bool VoiceInputController::prepare_audio(
    const std::array<uint8_t, VoiceCapturePipeline::kAdpcmBytesPerBlock>& adpcm,
    uint32_t captured_samples,
    uint32_t now_ms)
{
    bool overflow = false;
    bool exhausted = false;
    bool crypto_failed = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queued_audio_packets >= kMaxQueuedAudioPackets) {
            _session.record_queue_overflow();
            overflow = true;
        } else {
            uint32_t sequence = 0;
            if (!_session.allocate_audio_sequence(sequence)) {
                exhausted = true;
            } else {
                voice_input::VoiceNotification logical = {};
                logical.kind = voice_input::NotificationKind::Audio;
                logical.session = _session.session();
                logical.sequence = sequence;
                logical.watch_ms = now_ms;
                logical.session_nonce = _session_nonce;
                logical.payload_size = adpcm.size();
                std::copy(adpcm.begin(), adpcm.end(), logical.payload.begin());
                voice_input::Notification wire = {};
                if (!enqueue_encoded(logical, wire)) {
                    _session.record_encode_failure();
                    crypto_failed = true;
                } else {
                    _session.record_generated();
                    _session.record_captured_samples(captured_samples);
                    _outgoing.push_back(wire);
                    ++_queued_audio_packets;
                }
            }
        }
    }
    if (overflow) {
        fail(static_cast<uint16_t>(VoiceFailure::QueueOverflow), 0, now_ms);
        return false;
    }
    if (exhausted) {
        fail(static_cast<uint16_t>(VoiceFailure::SequenceExhausted), 0, now_ms);
        return false;
    }
    if (crypto_failed) {
        fail(static_cast<uint16_t>(VoiceFailure::Crypto), 0, now_ms);
        return false;
    }
    return true;
}

bool VoiceInputController::prepare_terminal(
    voice_input::NotificationKind kind,
    const uint8_t* payload,
    size_t payload_size,
    uint32_t now_ms)
{
    uint32_t sequence = 0;
    if (!_session.allocate_terminal_sequence(sequence) || payload == nullptr ||
        payload_size > voice_input::kAudioPlaintextSize) {
        return false;
    }
    return prepare_terminal_at(
        kind, payload, payload_size, sequence, now_ms);
}

bool VoiceInputController::prepare_terminal_at(
    voice_input::NotificationKind kind,
    const uint8_t* payload,
    size_t payload_size,
    uint32_t sequence,
    uint32_t now_ms)
{
    if (payload == nullptr || payload_size > voice_input::kAudioPlaintextSize) {
        return false;
    }
    voice_input::VoiceNotification logical = {};
    logical.kind = kind;
    logical.session = _session.session();
    logical.sequence = sequence;
    logical.watch_ms = now_ms;
    logical.session_nonce = _session_nonce;
    logical.payload_size = payload_size;
    std::copy_n(payload, payload_size, logical.payload.begin());
    voice_input::Notification wire = {};
    if (!enqueue_encoded(logical, wire)) {
        return false;
    }
    _outgoing.push_back(wire);
    _terminal_pending = true;
    return true;
}

void VoiceInputController::finalize_graceful_stop(uint32_t now_ms)
{
    bool terminal_failed = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_graceful_stop || _producer_running.load() || _terminal_pending ||
            !_outgoing.empty()) {
            return;
        }
        std::array<uint8_t, voice_input::kEndBodySize> payload = {};
        const voice_input::EndSummary summary = _session.summary(now_ms);
        const size_t size = voice_input::encode_end_summary(
            summary, payload.data(), payload.size());
        terminal_failed = size == 0 || !prepare_terminal(
            voice_input::NotificationKind::PttEnd,
            payload.data(),
            size,
            now_ms);
        _graceful_stop = false;
    }
    if (terminal_failed) {
        fail(
            static_cast<uint16_t>(VoiceFailure::Crypto),
            static_cast<uint16_t>(voice_input::NotificationKind::PttEnd),
            now_ms);
    }
}

void VoiceInputController::abort_with_failure(
    uint16_t code,
    uint16_t detail,
    uint32_t now_ms)
{
    _external_failure_requested = true;
    _capture_pipeline->requestStop();
    wake_producer();
    stop_capture_bounded();
    _capture_pipeline->close();
    fail(code, detail, now_ms);
    _external_failure_requested = false;
}

void VoiceInputController::fail(uint16_t code, uint16_t detail, uint32_t now_ms)
{
    _capture_pipeline->requestStop();
    std::lock_guard<std::mutex> lock(_mutex);
    voice_input::Notification pending_begin = {};
    const auto begin = std::find_if(
        _outgoing.begin(),
        _outgoing.end(),
        [](const voice_input::Notification& notification) {
            return notification.kind == voice_input::NotificationKind::PttBegin;
        });
    const bool preserve_begin = begin != _outgoing.end();
    if (preserve_begin) {
        pending_begin = *begin;
    }
    const bool can_emit_terminal = preserve_begin || _begin_accepted;
    _outgoing.clear();
    if (preserve_begin) {
        _outgoing.push_back(pending_begin);
    }
    _queued_audio_packets = 0;
    _graceful_stop = false;
    std::array<uint8_t, voice_input::kErrorBodySize> payload = {};
    const size_t size = voice_input::encode_error_body(
        voice_input::ErrorBody{code, detail}, payload.data(), payload.size());
    const uint32_t terminal_sequence = _has_accepted_notification
                                           ? _last_accepted_sequence + 1U
                                           : 1U;
    if (can_emit_terminal && _session.session() != 0 && size != 0 &&
        terminal_sequence != 0) {
        (void)prepare_terminal_at(
            voice_input::NotificationKind::VoiceError,
            payload.data(),
            size,
            terminal_sequence,
            now_ms);
    }
    _session.disconnect();
    if (_ready.load()) {
        _session.set_ready(true);
    }
    _begin_accepted = false;
    _ui_state = VoiceInputUiState::Error;
}

void VoiceInputController::stop_capture_bounded()
{
#if !defined(STOPWATCH_HOST_TEST)
    const int64_t deadline_us =
        esp_timer_get_time() + static_cast<int64_t>(kProducerStopTimeoutMs) * 1000;
    while (_producer_running.load() && esp_timer_get_time() < deadline_us) {
        vTaskDelay(1);
    }
    if (_producer_running.load() && _producer_task != nullptr) {
        xTaskAbortDelay(static_cast<TaskHandle_t>(_producer_task));
        wake_producer();
        const int64_t grace_deadline_us =
            esp_timer_get_time() + static_cast<int64_t>(kProducerCancelGraceMs) * 1000;
        while (_producer_running.load() && esp_timer_get_time() < grace_deadline_us) {
            vTaskDelay(1);
        }
    }
    if (_producer_running.load() && _producer_task != nullptr) {
        vTaskDelete(static_cast<TaskHandle_t>(_producer_task));
        _producer_task = nullptr;
        if (_producer_stopped != nullptr) {
            vSemaphoreDelete(static_cast<SemaphoreHandle_t>(_producer_stopped));
            _producer_stopped = nullptr;
        }
        _initialized = false;
    }
#else
    while (_producer_running.load()) {
        producer_step(0);
        if (_producer_running.load()) {
            std::this_thread::yield();
        }
    }
#endif
    _producer_running = false;
}

void VoiceInputController::reset_session(
    bool ready_after_reset,
    bool clear_notifications)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _session.disconnect();
    if (ready_after_reset) {
        _session.set_ready(true);
    }
    _session_nonce = {};
    if (clear_notifications) {
        _outgoing.clear();
    }
    _queued_audio_packets = 0;
    _graceful_stop = false;
    _terminal_pending = false;
    _begin_accepted = false;
    _has_accepted_notification = false;
    _last_accepted_sequence = 0;
    _congestion_retries = 0;
    _ui_state = ready_after_reset ? VoiceInputUiState::Ready
                                  : VoiceInputUiState::Off;
}

bool VoiceInputController::make_session_identity(
    uint16_t& session,
    voice_input::crypto::ExchangeNonce& nonce)
{
    session = 0;
    nonce = {};
    if (_next_session == 0) {
        return false;
    }
    for (int attempt = 0; attempt < 8; ++attempt) {
        const uint32_t low = _entropy->random32();
        const uint32_t high = _entropy->random32();
        for (size_t index = 0; index < 4; ++index) {
            nonce[index] = static_cast<uint8_t>((low >> (index * 8U)) & 0xffU);
            nonce[index + 4] =
                static_cast<uint8_t>((high >> (index * 8U)) & 0xffU);
        }
        const bool nonzero = std::any_of(
            nonce.begin(), nonce.end(), [](uint8_t byte) { return byte != 0; });
        if (nonzero && nonce != _last_session_nonce) {
            session = _next_session;
            _next_session = _next_session == UINT16_MAX
                                ? 0
                                : static_cast<uint16_t>(_next_session + 1U);
            _last_session_nonce = nonce;
            return true;
        }
    }
    return false;
}

bool VoiceInputController::enqueue_encoded(
    const voice_input::VoiceNotification& logical,
    voice_input::Notification& wire)
{
    wire = {};
    wire.kind = logical.kind;
    wire.session = logical.session;
    wire.sequence = logical.sequence;
    wire.size = voice_input::encode_notification(
        logical, *_crypto, wire.bytes.data(), wire.bytes.size());
    return wire.size != 0;
}

void VoiceInputController::wake_producer()
{
#if !defined(STOPWATCH_HOST_TEST)
    if (_producer_task != nullptr) {
        xTaskNotifyGive(static_cast<TaskHandle_t>(_producer_task));
    }
#endif
}
