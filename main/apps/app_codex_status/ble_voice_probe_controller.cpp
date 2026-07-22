#include "ble_voice_probe_controller.h"
#include "ima_adpcm_codec.h"

#include <hal/hal.h>
#include <hal/audio/stopwatch_audio_backend.h>
#include <esp_log.h>
#include <esp_ae_rate_cvt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <runtime/resource_supervisor.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace {

constexpr const char* kTag = "VoiceProbe";
constexpr size_t kVoicePacketQueueDepth = 32;
constexpr uint8_t kMaxConsecutiveQueueFailures = 3;
constexpr uint32_t kProducerTaskStackDepth = 4 * 1024;
constexpr UBaseType_t kProducerTaskPriority = 5;
constexpr TickType_t kProducerIdleDelay = pdMS_TO_TICKS(1);
constexpr TickType_t kCaptureReadTimeout = pdMS_TO_TICKS(100);
constexpr TickType_t kProducerStopTimeout = pdMS_TO_TICKS(500);
constexpr TickType_t kProducerCancelGraceTimeout = pdMS_TO_TICKS(250);
static_assert(kProducerStopTimeout > kCaptureReadTimeout,
              "worker stop timeout must exceed the maximum capture read timeout");
static_assert(kProducerCancelGraceTimeout > kCaptureReadTimeout,
              "worker cancellation grace must exceed capture read timeout");
constexpr uint16_t kMinimumNotificationLimit =
    ble_voice_probe::kNotificationHeaderSize + ble_voice_probe::kStopSummarySize;
constexpr uint32_t kCaptureRate = 44100;
constexpr uint32_t kOutputRate = 16000;
constexpr size_t kCaptureSamplesPerBlock = 882;
constexpr size_t kOutputSamplesPerBlock = 320;
constexpr size_t kAdpcmBytesPerBlock = 164;
constexpr uint16_t kMicrophoneNotificationLimit =
    ble_voice_probe::kNotificationHeaderSize + kAdpcmBytesPerBlock;
constexpr uint32_t kMaxResamplerOutputSamples = 512;
constexpr uint16_t kFallbackErrorSession = 0xffff;

class StateLock {
public:
    explicit StateLock(void* mutex)
        : _mutex(static_cast<SemaphoreHandle_t>(mutex)),
          _locked(_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE)
    {
    }

    ~StateLock()
    {
        if (_locked) {
            xSemaphoreGive(_mutex);
        }
    }

    bool locked() const
    {
        return _locked;
    }

private:
    SemaphoreHandle_t _mutex = nullptr;
    bool _locked = false;
};

}  // namespace

BleVoiceProbeController::BleVoiceProbeController() = default;

BleVoiceProbeController::~BleVoiceProbeController()
{
    shutdown();
}

bool BleVoiceProbeController::initialize()
{
    if (_initialized) {
        return true;
    }

    _packet_queue = xQueueCreate(kVoicePacketQueueDepth, sizeof(ble_voice_probe::Notification));
    _state_mutex = xSemaphoreCreateMutex();
    _producer_stopped = xSemaphoreCreateBinary();
    if (_packet_queue == nullptr || _state_mutex == nullptr || _producer_stopped == nullptr) {
        shutdown();
        return false;
    }

    _shutdown_requested = false;
    TaskHandle_t task = nullptr;
    if (xTaskCreate(
            BleVoiceProbeController::producer_entry,
            "voice_probe",
            kProducerTaskStackDepth,
            this,
            kProducerTaskPriority,
            &task) != pdPASS) {
        shutdown();
        return false;
    }
    _producer_task = task;
    _initialized = true;
    return true;
}

void BleVoiceProbeController::handle_control(
    const CodexBleVoiceControl& frame,
    bool subscribed,
    uint16_t negotiated_mtu,
    uint32_t now_ms
)
{
    {
        StateLock lock(_state_mutex);
        if (!lock.locked() || _shutdown_requested.load()) {
            return;
        }
        _link_active = true;
    }

    ble_voice_probe::ControlCommand command;
    if (!ble_voice_probe::decode_control(frame.data.data(), frame.length, command)) {
        enqueue_error(
            ble_voice_probe::ErrorCode::InvalidCommand,
            session_for_error(frame),
            0,
            now_ms);
        return;
    }

    if (command.command == ble_voice_probe::Command::Stop) {
        bool matches_active_session = false;
        {
            StateLock lock(_state_mutex);
            matches_active_session = lock.locked() && _session.active() &&
                                     _session.id() == command.session;
        }
        if (!matches_active_session) {
            enqueue_error(
                ble_voice_probe::ErrorCode::InvalidCommand,
                command.session,
                0,
                now_ms);
            return;
        }
        request_stop(true, ble_voice_probe::ErrorCode::InvalidCommand);
        return;
    }

    if (!subscribed) {
        enqueue_error(
            ble_voice_probe::ErrorCode::NotSubscribed,
            command.session,
            0,
            now_ms);
        return;
    }
    if (command.mode != ble_voice_probe::Mode::Synthetic &&
        command.mode != ble_voice_probe::Mode::MicrophoneAdpcm) {
        enqueue_error(
            ble_voice_probe::ErrorCode::InvalidCommand,
            command.session,
            static_cast<uint16_t>(command.mode),
            now_ms);
        return;
    }
    if (notification_limit(negotiated_mtu) < kMinimumNotificationLimit) {
        enqueue_error(
            ble_voice_probe::ErrorCode::MtuTooSmall,
            command.session,
            negotiated_mtu,
            now_ms);
        return;
    }
    if (command.mode == ble_voice_probe::Mode::MicrophoneAdpcm &&
        notification_limit(negotiated_mtu) < kMicrophoneNotificationLimit) {
        enqueue_error(
            ble_voice_probe::ErrorCode::MtuTooSmall,
            command.session,
            negotiated_mtu,
            now_ms);
        return;
    }

    if (!start_session(command, negotiated_mtu, now_ms)) {
        enqueue_error(ble_voice_probe::ErrorCode::Busy, command.session, 0, now_ms);
    }
}

void BleVoiceProbeController::poll(uint32_t now_ms)
{
    retry_start_ack();
    retry_terminal_error();
    finalize_stop(now_ms);
}

bool BleVoiceProbeController::take_notification(ble_voice_probe::Notification& packet)
{
    if (_packet_queue == nullptr) {
        return false;
    }
    return xQueueReceive(
               static_cast<QueueHandle_t>(_packet_queue),
               &packet,
               0) == pdTRUE;
}

void BleVoiceProbeController::record_notify_result(
    ble_voice_probe::Kind kind,
    CodexBleNotifyResult result,
    uint32_t now_ms
)
{
    bool stop_for_congestion = false;
    bool wake_producer = false;
    {
        StateLock lock(_state_mutex);
        if (!lock.locked()) {
            return;
        }

        if (result == CodexBleNotifyResult::Accepted) {
            _consecutive_failures = 0;
            if (kind == ble_voice_probe::Kind::StartAck && _start_ack_queued) {
                _start_ack_queued = false;
                _start_ack_pending = false;
                wake_producer = true;
            }
            if (kind == ble_voice_probe::Kind::Data && _pending_data_queued &&
                _session.active()) {
                _session.record_notify_accepted();
                _pending_data = {};
                _pending_data_valid = false;
                _pending_data_queued = false;
                _pending_data_counted = false;
                wake_producer = true;
            }
            if (kind == ble_voice_probe::Kind::StopSummary && _summary_queued) {
                if (_terminal_error_pending) {
                    _pending_summary = {};
                    _summary_queued = false;
                    _summary_prepared = false;
                } else {
                    reset_session_locked();
                }
            }
            if (kind == ble_voice_probe::Kind::Error && _terminal_error_queued) {
                reset_session_locked();
            }
        } else {
            bool retryable_packet = false;
            if (kind == ble_voice_probe::Kind::StartAck && _start_ack_queued) {
                _start_ack_queued = false;
                retryable_packet = true;
                wake_producer = true;
            }
            if (kind == ble_voice_probe::Kind::Data && _pending_data_queued) {
                _pending_data_queued = false;
                retryable_packet = true;
                wake_producer = true;
            }
            if (kind == ble_voice_probe::Kind::StopSummary && _summary_queued) {
                _summary_queued = false;
                retryable_packet = true;
            }
            if (kind == ble_voice_probe::Kind::Error && _terminal_error_queued) {
                _terminal_error_queued = false;
                wake_producer = true;
            } else if (retryable_packet) {
                if (_terminal_error_pending) {
                    replace_terminal_error_sequence_locked(kind);
                    wake_producer = true;
                } else {
                    ++_consecutive_failures;
                    stop_for_congestion =
                        _consecutive_failures >= kMaxConsecutiveQueueFailures;
                }
            }
        }
    }

    if (wake_producer && _producer_task != nullptr) {
        xTaskNotifyGive(static_cast<TaskHandle_t>(_producer_task));
    }
    if (stop_for_congestion) {
        request_stop(false, ble_voice_probe::ErrorCode::QueueCongested);
    }
    (void)now_ms;
}

void BleVoiceProbeController::handle_disconnect()
{
    {
        StateLock lock(_state_mutex);
        if (!lock.locked()) {
            return;
        }
        _link_active = false;
        reset_session_locked();
        if (_packet_queue != nullptr) {
            xQueueReset(static_cast<QueueHandle_t>(_packet_queue));
        }
    }
    if (_producer_task != nullptr) {
        xTaskNotifyGive(static_cast<TaskHandle_t>(_producer_task));
    }
}

void BleVoiceProbeController::shutdown()
{
    _shutdown_requested = true;
    if (_producer_task != nullptr) {
        TaskHandle_t worker = static_cast<TaskHandle_t>(_producer_task);
        xTaskNotifyGive(worker);
        bool worker_stopped = _producer_stopped != nullptr &&
                              xSemaphoreTake(
                                  static_cast<SemaphoreHandle_t>(_producer_stopped),
                                  kProducerStopTimeout) == pdTRUE;
        if (!worker_stopped) {
            ESP_LOGW(kTag, "voice probe worker exceeded 500 ms stop barrier; aborting bounded wait");
            xTaskAbortDelay(worker);
            xTaskNotifyGive(worker);
            worker_stopped = _producer_stopped != nullptr &&
                             xSemaphoreTake(
                                 static_cast<SemaphoreHandle_t>(_producer_stopped),
                                 kProducerCancelGraceTimeout) == pdTRUE;
            if (!worker_stopped) {
                ESP_LOGW(kTag, "voice probe worker exceeded cancellation grace; forcing deletion");
            }
        }
        // read() holds no backend mutex while its I2S wait is active. The abort
        // plus grace period exceeds that wait's 100 ms bound, so forced deletion
        // cannot strand an audio mutex or the I2S driver's read-side binary lock.
        vTaskDelete(worker);
        _producer_task = nullptr;
    }

    release_audio_resources(true);
    _link_active = false;
    reset_session_locked();
    if (_packet_queue != nullptr) {
        vQueueDelete(static_cast<QueueHandle_t>(_packet_queue));
        _packet_queue = nullptr;
    }
    if (_producer_stopped != nullptr) {
        vSemaphoreDelete(static_cast<SemaphoreHandle_t>(_producer_stopped));
        _producer_stopped = nullptr;
    }
    if (_state_mutex != nullptr) {
        vSemaphoreDelete(static_cast<SemaphoreHandle_t>(_state_mutex));
        _state_mutex = nullptr;
    }
    _initialized = false;
}

void BleVoiceProbeController::producer_entry(void* context)
{
    static_cast<BleVoiceProbeController*>(context)->producer_loop();
}

void BleVoiceProbeController::producer_loop()
{
    while (!_shutdown_requested.load()) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (!_shutdown_requested.load()) {
            bool stop_for_duration = false;
            bool have_packet = false;
            bool setup_microphone = false;
            bool prepare_microphone = false;
            bool capture_microphone = false;
            bool release_microphone = false;
            bool wait_for_state_change = false;
            bool producer_inactive = false;
            {
                StateLock lock(_state_mutex);
                if (!lock.locked()) {
                    break;
                }
                if (!_session.active() || !_producer_enabled || _terminal_error_pending) {
                    producer_inactive = true;
                    release_microphone = _microphone_cleanup_pending;
                } else if (_microphone_setup_pending) {
                    _microphone_setup_pending = false;
                    setup_microphone = true;
                } else if (_start_ack_pending || _start_ack_queued) {
                    wait_for_state_change = true;
                } else if (_pending_data_valid) {
                    have_packet = !_pending_data_queued;
                    wait_for_state_change = _pending_data_queued;
                } else if (_session.mode() == ble_voice_probe::Mode::MicrophoneAdpcm &&
                           _microphone_pcm.has_complete_block()) {
                    prepare_microphone = true;
                } else if (_stop_requested) {
                    _producer_enabled = false;
                    producer_inactive = true;
                    release_microphone = _microphone_cleanup_pending;
                } else {
                    const uint32_t now_ms = GetHAL().millis();
                    if (_session.duration_reached(now_ms)) {
                        stop_for_duration = true;
                    } else if (_session.mode() == ble_voice_probe::Mode::Synthetic) {
                        const uint32_t due = _session.synthetic_bytes_due(now_ms);
                        if (due != 0 && _notification_payload_capacity != 0) {
                            const uint16_t payload_length = static_cast<uint16_t>(std::min<uint32_t>(
                                due, _notification_payload_capacity));
                            _pending_data = {};
                            _pending_data.kind = ble_voice_probe::Kind::Data;
                            _pending_data.session = _session.id();
                            _pending_data.sequence = _session.allocate_sequence();
                            _pending_data.watch_ms = now_ms;
                            _pending_data.payload_length = payload_length;
                            ble_voice_probe::fill_synthetic_payload(
                                _pending_data.sequence,
                                _pending_data.payload.data(),
                                payload_length);
                            _pending_data_valid = true;
                            _pending_data_queued = false;
                            _pending_data_counted = false;
                            have_packet = true;
                        }
                    } else {
                        capture_microphone = true;
                    }
                }
            }

            if (release_microphone) {
                release_audio_resources();
                break;
            }
            if (producer_inactive) {
                break;
            }
            if (setup_microphone) {
                ble_voice_probe::ErrorCode error = ble_voice_probe::ErrorCode::ResamplerFailed;
                if (!setup_microphone_resources(error)) {
                    fail_microphone_session(error);
                    break;
                }

                bool setup_cancelled = false;
                {
                    StateLock lock(_state_mutex);
                    if (!lock.locked() || !_session.active() || !_producer_enabled || _stop_requested ||
                        _session.mode() != ble_voice_probe::Mode::MicrophoneAdpcm ||
                        !queue_start_ack_locked(
                            _negotiated_mtu,
                            _session.mode(),
                            _session.target_bps(),
                            GetHAL().millis())) {
                        setup_cancelled = true;
                    }
                }
                if (setup_cancelled) {
                    release_audio_resources();
                    break;
                }
                continue;
            }
            if (stop_for_duration) {
                request_stop(true, ble_voice_probe::ErrorCode::InvalidCommand);
                continue;
            }
            if (prepare_microphone) {
                ble_voice_probe::ErrorCode error = ble_voice_probe::ErrorCode::EncoderFailed;
                if (!prepare_microphone_packet(error)) {
                    StateLock lock(_state_mutex);
                    if (lock.locked() && _session.active()) {
                        _session.record_encode_failure();
                    }
                    fail_microphone_session(error);
                    break;
                }
                continue;
            }
            if (capture_microphone) {
                ble_voice_probe::ErrorCode error = ble_voice_probe::ErrorCode::ResamplerFailed;
                if (!capture_microphone_samples(error)) {
                    bool stopping = false;
                    {
                        StateLock lock(_state_mutex);
                        stopping = lock.locked() && _stop_requested;
                    }
                    if (!stopping) {
                        fail_microphone_session(error);
                        break;
                    }
                }
                continue;
            }
            if (wait_for_state_change) {
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                continue;
            }
            if (!have_packet) {
                ulTaskNotifyTake(pdTRUE, kProducerIdleDelay);
                continue;
            }

            ble_voice_probe::Notification packet;
            bool enqueued = false;
            bool stop_for_congestion = false;
            {
                StateLock lock(_state_mutex);
                if (!lock.locked() || !_session.active() || !_pending_data_valid ||
                    _pending_data_queued || _terminal_error_pending) {
                    continue;
                }
                packet = _pending_data;
                const bool microphone_packet =
                    _session.mode() == ble_voice_probe::Mode::MicrophoneAdpcm &&
                    packet.payload_length == kAdpcmBytesPerBlock;
                enqueued = enqueue_packet(packet);
                if (enqueued) {
                    _pending_data_queued = true;
                    if (!_pending_data_counted) {
                        _session.record_generated(packet.payload_length);
                        if (microphone_packet) {
                            _session.record_captured_samples(kOutputSamplesPerBlock);
                        }
                        _pending_data_counted = true;
                    }
                } else {
                    _session.record_queue_overflow();
                    ++_consecutive_failures;
                    stop_for_congestion = _consecutive_failures >= kMaxConsecutiveQueueFailures;
                }
            }
            if (stop_for_congestion) {
                request_stop(false, ble_voice_probe::ErrorCode::QueueCongested);
                break;
            }
            if (!enqueued) {
                ulTaskNotifyTake(pdTRUE, kProducerIdleDelay);
            }
        }
    }

    release_audio_resources();
    if (_producer_stopped != nullptr) {
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(_producer_stopped));
    }
    // shutdown() owns task deletion. Suspending prevents a self-delete race with it.
    for (;;) {
        vTaskSuspend(nullptr);
    }
}

bool BleVoiceProbeController::queue_start_ack_locked(
    uint16_t negotiated_mtu,
    ble_voice_probe::Mode mode,
    uint32_t target_bps,
    uint32_t now_ms
)
{
    if (!_session.active()) {
        return false;
    }

    _pending_start_ack = {};
    _pending_start_ack.kind = ble_voice_probe::Kind::StartAck;
    _pending_start_ack.session = _session.id();
    _pending_start_ack.sequence = _session.allocate_sequence();
    _pending_start_ack.watch_ms = now_ms;
    _pending_start_ack.payload_length = ble_voice_probe::encode_start_ack(
        negotiated_mtu,
        mode,
        target_bps,
        _pending_start_ack.payload.data(),
        _pending_start_ack.payload.size());
    if (_pending_start_ack.payload_length == 0) {
        return false;
    }

    _start_ack_pending = true;
    if (enqueue_packet(_pending_start_ack)) {
        _start_ack_queued = true;
    } else {
        _session.record_queue_overflow();
        ++_consecutive_failures;
    }
    return true;
}

bool BleVoiceProbeController::setup_microphone_resources(ble_voice_probe::ErrorCode& error)
{
    if (GetResourceSupervisor().acquireCapture(RuntimeOwner::Codex) != AcquireResult::Granted) {
        error = ble_voice_probe::ErrorCode::CaptureBusy;
        return false;
    }
    _capture_owned = true;

    if (!GetStopWatchAudioBackend().openRecorder(44100, 16, 1)) {
        error = ble_voice_probe::ErrorCode::RecorderOpenFailed;
        release_audio_resources();
        return false;
    }
    _recorder_open = true;

    esp_ae_rate_cvt_cfg_t config = {};
    config.src_rate = kCaptureRate;
    config.dest_rate = kOutputRate;
    config.channel = 1;
    config.bits_per_sample = 16;
    config.complexity = 3;
    config.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY;
    if (esp_ae_rate_cvt_open(&config, &_resampler) != ESP_AE_ERR_OK || _resampler == nullptr) {
        error = ble_voice_probe::ErrorCode::ResamplerFailed;
        release_audio_resources();
        return false;
    }

    uint32_t output_capacity = 0;
    if (esp_ae_rate_cvt_get_max_out_sample_num(
            _resampler,
            kCaptureSamplesPerBlock,
            &output_capacity) != ESP_AE_ERR_OK ||
        output_capacity == 0 || output_capacity > kMaxResamplerOutputSamples) {
        error = ble_voice_probe::ErrorCode::ResamplerFailed;
        release_audio_resources();
        return false;
    }

    _resampler_output_capacity = output_capacity;
    _resampler_output.assign(_resampler_output_capacity, 0);
    _microphone_pcm.clear();
    return true;
}

bool BleVoiceProbeController::capture_microphone_samples(ble_voice_probe::ErrorCode& error)
{
    if (_resampler == nullptr || !_recorder_open || _resampler_output_capacity == 0 ||
        _resampler_output.size() != _resampler_output_capacity) {
        error = ble_voice_probe::ErrorCode::ResamplerFailed;
        return false;
    }

    std::array<int16_t, kCaptureSamplesPerBlock> captured = {};
    const size_t expected_bytes = captured.size() * sizeof(captured.front());
    if (GetStopWatchAudioBackend().read(captured.data(), expected_bytes, kCaptureReadTimeout) != expected_bytes) {
        error = ble_voice_probe::ErrorCode::RecorderOpenFailed;
        return false;
    }

    uint32_t output_samples = _resampler_output_capacity;
    if (esp_ae_rate_cvt_process(
            _resampler,
            static_cast<esp_ae_sample_t>(captured.data()),
            captured.size(),
            static_cast<esp_ae_sample_t>(_resampler_output.data()),
            &output_samples) != ESP_AE_ERR_OK ||
        output_samples > _resampler_output_capacity) {
        error = ble_voice_probe::ErrorCode::ResamplerFailed;
        return false;
    }

    _microphone_pcm.append(_resampler_output.data(), output_samples);
    return true;
}

bool BleVoiceProbeController::prepare_microphone_packet(ble_voice_probe::ErrorCode& error)
{
    ble_voice_probe::detail::MicrophonePcmBuffer::Block samples = {};
    if (!_microphone_pcm.take_complete_block(samples)) {
        return true;
    }

    std::array<uint8_t, kAdpcmBytesPerBlock> encoded = {};
    if (ble_voice_probe::encode_ima_adpcm_block(
            samples.data(),
            samples.size(),
            encoded.data(),
            encoded.size()) != kAdpcmBytesPerBlock) {
        error = ble_voice_probe::ErrorCode::EncoderFailed;
        return false;
    }

    {
        StateLock lock(_state_mutex);
        if (!lock.locked() || !_session.active() || !_producer_enabled ||
            _terminal_error_pending || _pending_data_valid ||
            _session.mode() != ble_voice_probe::Mode::MicrophoneAdpcm) {
            return true;
        }
        _pending_data = {};
        _pending_data.kind = ble_voice_probe::Kind::Data;
        _pending_data.session = _session.id();
        _pending_data.sequence = _session.allocate_sequence();
        _pending_data.watch_ms = GetHAL().millis();
        _pending_data.payload_length = kAdpcmBytesPerBlock;
        std::copy(encoded.begin(), encoded.end(), _pending_data.payload.begin());
        _pending_data_valid = true;
        _pending_data_queued = false;
        _pending_data_counted = false;
    }
    return true;
}

void BleVoiceProbeController::fail_microphone_session(ble_voice_probe::ErrorCode error)
{
    {
        StateLock lock(_state_mutex);
        if (lock.locked() && _session.active() &&
            _session.mode() == ble_voice_probe::Mode::MicrophoneAdpcm) {
            prepare_terminal_error_locked(error, 0, GetHAL().millis());
        }
    }

    release_audio_resources();
}

void BleVoiceProbeController::release_audio_resources(bool forced_cleanup)
{
    if (_shutdown_requested.load() && !forced_cleanup) {
        return;
    }
    if (_resampler != nullptr) {
        esp_ae_rate_cvt_close(_resampler);
        _resampler = nullptr;
    }
    if (_recorder_open) {
        GetStopWatchAudioBackend().closeRecorder();
        _recorder_open = false;
    }
    if (_capture_owned) {
        GetResourceSupervisor().releaseCapture(RuntimeOwner::Codex);
        _capture_owned = false;
    }
    _resampler_output.clear();
    _microphone_pcm.clear();
    _resampler_output_capacity = 0;

    if (forced_cleanup) {
        _microphone_setup_pending = false;
        _microphone_cleanup_pending = false;
        return;
    }

    StateLock lock(_state_mutex);
    if (lock.locked()) {
        _microphone_setup_pending = false;
        _microphone_cleanup_pending = false;
    }
}

bool BleVoiceProbeController::start_session(
    const ble_voice_probe::ControlCommand& command,
    uint16_t negotiated_mtu,
    uint32_t now_ms
)
{
    {
        StateLock lock(_state_mutex);
        if (!lock.locked()) {
            return false;
        }
        if (_session.active() || _summary_queued || _terminal_error_pending ||
            _microphone_cleanup_pending ||
            (_packet_queue != nullptr && uxQueueMessagesWaiting(static_cast<QueueHandle_t>(_packet_queue)) != 0) ||
            !_session.start(command, now_ms, negotiated_mtu)) {
            return false;
        }

        _notification_payload_capacity = static_cast<uint16_t>(
            notification_limit(negotiated_mtu) - ble_voice_probe::kNotificationHeaderSize);
        _negotiated_mtu = negotiated_mtu;
        _producer_enabled = true;
        if (command.mode == ble_voice_probe::Mode::MicrophoneAdpcm) {
            _microphone_setup_pending = true;
            _microphone_cleanup_pending = true;
        } else if (!queue_start_ack_locked(
                       negotiated_mtu,
                       command.mode,
                       command.target_bps,
                       now_ms)) {
            reset_session_locked();
            return false;
        } else {
            _microphone_setup_pending = false;
        }
    }

    if (_producer_task != nullptr) {
        xTaskNotifyGive(static_cast<TaskHandle_t>(_producer_task));
    }
    return true;
}

void BleVoiceProbeController::request_stop(bool emit_summary, ble_voice_probe::ErrorCode reason)
{
    {
        StateLock lock(_state_mutex);
        if (!lock.locked() || !_session.active()) {
            return;
        }

        if (emit_summary) {
            _stop_requested = true;
        } else {
            prepare_terminal_error_locked(reason, 0, GetHAL().millis());
        }
    }

    if (_producer_task != nullptr) {
        xTaskNotifyGive(static_cast<TaskHandle_t>(_producer_task));
    }
}

bool BleVoiceProbeController::enqueue_packet(ble_voice_probe::Notification& packet)
{
    if (_packet_queue == nullptr || packet.session == 0 ||
        packet.payload_length > packet.payload.size()) {
        return false;
    }
    return xQueueSend(
               static_cast<QueueHandle_t>(_packet_queue),
               &packet,
               0) == pdTRUE;
}

void BleVoiceProbeController::retry_start_ack()
{
    bool stop_for_congestion = false;
    {
        StateLock lock(_state_mutex);
        if (!lock.locked() || !_session.active() || _terminal_error_pending ||
            !_start_ack_pending || _start_ack_queued) {
            return;
        }
        if (enqueue_packet(_pending_start_ack)) {
            _start_ack_queued = true;
        } else {
            _session.record_queue_overflow();
            ++_consecutive_failures;
            stop_for_congestion = _consecutive_failures >= kMaxConsecutiveQueueFailures;
        }
    }
    if (stop_for_congestion) {
        request_stop(false, ble_voice_probe::ErrorCode::QueueCongested);
    }
}

void BleVoiceProbeController::retry_terminal_error()
{
    StateLock lock(_state_mutex);
    if (!lock.locked() || !_session.active() || !_terminal_error_pending ||
        _terminal_error_queued || _microphone_cleanup_pending ||
        _start_ack_pending || _start_ack_queued ||
        _pending_data_valid || _pending_data_queued ||
        _summary_prepared || _summary_queued ||
        _packet_queue == nullptr ||
        uxQueueMessagesWaiting(static_cast<QueueHandle_t>(_packet_queue)) != 0) {
        return;
    }
    if (enqueue_packet(_pending_error)) {
        _terminal_error_queued = true;
    }
}

void BleVoiceProbeController::finalize_stop(uint32_t now_ms)
{
    bool stop_for_congestion = false;
    {
        StateLock lock(_state_mutex);
        if (!lock.locked() || !_session.active() || !_stop_requested ||
            _terminal_error_pending || _microphone_cleanup_pending || _start_ack_pending ||
            _start_ack_queued || _summary_queued ||
            _pending_data_valid || _packet_queue == nullptr ||
            uxQueueMessagesWaiting(static_cast<QueueHandle_t>(_packet_queue)) != 0) {
            return;
        }

        if (!_summary_prepared) {
            _pending_summary = {};
            _pending_summary.kind = ble_voice_probe::Kind::StopSummary;
            _pending_summary.session = _session.id();
            _pending_summary.sequence = _session.allocate_sequence();
            _pending_summary.watch_ms = now_ms;
            _pending_summary.payload_length = ble_voice_probe::encode_stop_summary(
                _session.summary(now_ms),
                _pending_summary.payload.data(),
                _pending_summary.payload.size());
            if (_pending_summary.payload_length == 0) {
                return;
            }
            _summary_prepared = true;
        }
        if (!enqueue_packet(_pending_summary)) {
            _session.record_queue_overflow();
            ++_consecutive_failures;
            stop_for_congestion = _consecutive_failures >= kMaxConsecutiveQueueFailures;
        } else {
            _summary_queued = true;
        }
    }
    if (stop_for_congestion) {
        request_stop(false, ble_voice_probe::ErrorCode::QueueCongested);
    }
}

void BleVoiceProbeController::prepare_terminal_error_locked(
    ble_voice_probe::ErrorCode code,
    uint16_t detail,
    uint32_t now_ms
)
{
    if (!_session.active() || _terminal_error_pending) {
        return;
    }

    const bool replace_pending_data =
        _pending_data_valid && !_pending_data_queued;
    const bool replace_pending_start_ack =
        _start_ack_pending && !_start_ack_queued;
    const bool replace_pending_summary =
        _summary_prepared && !_summary_queued;

    uint32_t sequence = 0;
    if (replace_pending_data) {
        sequence = _pending_data.sequence;
    } else if (replace_pending_start_ack) {
        sequence = _pending_start_ack.sequence;
    } else if (replace_pending_summary) {
        sequence = _pending_summary.sequence;
    } else {
        sequence = _session.allocate_sequence();
    }

    _pending_error = {};
    _pending_error.kind = ble_voice_probe::Kind::Error;
    _pending_error.session = _session.id();
    _pending_error.sequence = sequence;
    _pending_error.watch_ms = now_ms;
    _pending_error.payload_length = ble_voice_probe::encode_error(
        code,
        detail,
        _pending_error.payload.data(),
        _pending_error.payload.size());

    _producer_enabled = false;
    _stop_requested = false;
    if (replace_pending_start_ack) {
        _pending_start_ack = {};
        _start_ack_pending = false;
        _start_ack_queued = false;
    }
    if (replace_pending_data) {
        _pending_data = {};
        _pending_data_valid = false;
        _pending_data_queued = false;
        _pending_data_counted = false;
    }
    if (replace_pending_summary) {
        _pending_summary = {};
        _summary_prepared = false;
        _summary_queued = false;
    }
    _terminal_error_pending = _pending_error.payload_length != 0;
    _terminal_error_queued = false;
}

void BleVoiceProbeController::replace_terminal_error_sequence_locked(
    ble_voice_probe::Kind kind
)
{
    if (!_terminal_error_pending || _terminal_error_queued) {
        return;
    }
    if (kind == ble_voice_probe::Kind::StartAck && _start_ack_pending &&
        !_start_ack_queued) {
        _pending_error.sequence = _pending_start_ack.sequence;
        _pending_start_ack = {};
        _start_ack_pending = false;
        return;
    }
    if (kind == ble_voice_probe::Kind::Data && _pending_data_valid &&
        !_pending_data_queued) {
        _pending_error.sequence = _pending_data.sequence;
        _pending_data = {};
        _pending_data_valid = false;
        _pending_data_counted = false;
        return;
    }
    if (kind == ble_voice_probe::Kind::StopSummary && _summary_prepared &&
        !_summary_queued) {
        _pending_error.sequence = _pending_summary.sequence;
        _pending_summary = {};
        _summary_prepared = false;
    }
}

void BleVoiceProbeController::enqueue_error(
    ble_voice_probe::ErrorCode code,
    uint16_t session,
    uint16_t detail,
    uint32_t now_ms,
    uint32_t sequence
)
{
    StateLock lock(_state_mutex);
    if (!lock.locked() || !_link_active || _shutdown_requested.load()) {
        return;
    }

    ble_voice_probe::Notification packet;
    packet.kind = ble_voice_probe::Kind::Error;
    packet.session = session == 0 ? kFallbackErrorSession : session;
    packet.sequence = sequence;
    packet.watch_ms = now_ms;
    packet.payload_length = ble_voice_probe::encode_error(
        code,
        detail,
        packet.payload.data(),
        packet.payload.size());
    enqueue_packet(packet);
}

uint16_t BleVoiceProbeController::session_for_error(const CodexBleVoiceControl& frame) const
{
    if (frame.length >= 6) {
        const uint16_t session = static_cast<uint16_t>(frame.data[4]) |
                                 static_cast<uint16_t>(frame.data[5] << 8U);
        if (session != 0) {
            return session;
        }
    }
    return kFallbackErrorSession;
}

uint16_t BleVoiceProbeController::notification_limit(uint16_t negotiated_mtu) const
{
    if (negotiated_mtu <= 3) {
        return 0;
    }
    return static_cast<uint16_t>(std::min<size_t>(
        ble_voice_probe::kMaxNotificationSize,
        static_cast<size_t>(negotiated_mtu - 3)));
}

void BleVoiceProbeController::reset_session_locked()
{
    _session.reset();
    _notification_payload_capacity = 0;
    _negotiated_mtu = 0;
    _consecutive_failures = 0;
    _producer_enabled = false;
    _stop_requested = false;
    _start_ack_pending = false;
    _start_ack_queued = false;
    _summary_queued = false;
    _summary_prepared = false;
    _pending_data_valid = false;
    _pending_data_queued = false;
    _pending_data_counted = false;
    _terminal_error_pending = false;
    _terminal_error_queued = false;
    _microphone_setup_pending = false;
    _pending_start_ack = {};
    _pending_data = {};
    _pending_summary = {};
    _pending_error = {};
}
