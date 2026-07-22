#include "ble_voice_probe_controller.h"

#include <hal/hal.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstddef>

namespace {

constexpr const char* kTag = "VoiceProbe";
constexpr size_t kVoicePacketQueueDepth = 32;
constexpr uint8_t kMaxConsecutiveQueueFailures = 3;
constexpr uint32_t kProducerTaskStackDepth = 4 * 1024;
constexpr UBaseType_t kProducerTaskPriority = 5;
constexpr TickType_t kProducerIdleDelay = pdMS_TO_TICKS(1);
constexpr TickType_t kProducerStopTimeout = pdMS_TO_TICKS(1000);
constexpr uint16_t kMinimumNotificationLimit =
    ble_voice_probe::kNotificationHeaderSize + ble_voice_probe::kStopSummarySize;
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
    if (command.mode != ble_voice_probe::Mode::Synthetic) {
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

    if (!start_session(command, negotiated_mtu, now_ms)) {
        enqueue_error(ble_voice_probe::ErrorCode::Busy, command.session, 0, now_ms);
    }
}

void BleVoiceProbeController::poll(uint32_t now_ms)
{
    retry_start_ack();
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
            if (kind == ble_voice_probe::Kind::Data && _session.active()) {
                _session.record_notify_accepted();
            }
            if (kind == ble_voice_probe::Kind::StopSummary && _summary_queued) {
                reset_session_locked();
            }
        } else {
            ++_consecutive_failures;
            if (kind == ble_voice_probe::Kind::StartAck && _start_ack_queued) {
                _start_ack_queued = false;
            }
            if (kind == ble_voice_probe::Kind::StopSummary && _summary_queued) {
                _summary_queued = false;
            }
            stop_for_congestion = _consecutive_failures >= kMaxConsecutiveQueueFailures;
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
        xTaskNotifyGive(static_cast<TaskHandle_t>(_producer_task));
        if (_producer_stopped != nullptr &&
            xSemaphoreTake(static_cast<SemaphoreHandle_t>(_producer_stopped), kProducerStopTimeout) != pdTRUE) {
            ESP_LOGW(kTag, "voice probe worker stop timed out");
        }
        // The worker owns no transport, NimBLE, or hardware resource. It only accesses
        // this controller's queue and state mutex, so deleting it is the final ownership
        // barrier before those objects are released.
        vTaskDelete(static_cast<TaskHandle_t>(_producer_task));
        _producer_task = nullptr;
    }

    // No worker remains after the controller-owned task deletion above.
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
            {
                StateLock lock(_state_mutex);
                if (!lock.locked() || !_producer_enabled || !_session.active() || _stop_requested ||
                    _start_ack_pending || _start_ack_queued) {
                    break;
                }

                const uint32_t now_ms = GetHAL().millis();
                if (_session.duration_reached(now_ms)) {
                    stop_for_duration = true;
                } else if (_pending_data_valid) {
                    have_packet = true;
                } else {
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
                        have_packet = true;
                    }
                }
            }

            if (stop_for_duration) {
                request_stop(true, ble_voice_probe::ErrorCode::InvalidCommand);
                break;
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
                if (!lock.locked() || !_session.active() || !_pending_data_valid || _stop_requested) {
                    continue;
                }
                packet = _pending_data;
                enqueued = enqueue_packet(packet);
                if (enqueued) {
                    _session.record_generated(packet.payload_length);
                    _pending_data_valid = false;
                    _consecutive_failures = 0;
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

    if (_producer_stopped != nullptr) {
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(_producer_stopped));
    }
    // shutdown() owns task deletion. Suspending prevents a self-delete race with it.
    for (;;) {
        vTaskSuspend(nullptr);
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
        if (_session.active() || _summary_queued ||
            (_packet_queue != nullptr && uxQueueMessagesWaiting(static_cast<QueueHandle_t>(_packet_queue)) != 0) ||
            !_session.start(command, now_ms, negotiated_mtu)) {
            return false;
        }

        _notification_payload_capacity = static_cast<uint16_t>(
            notification_limit(negotiated_mtu) - ble_voice_probe::kNotificationHeaderSize);
        _pending_start_ack = {};
        _pending_start_ack.kind = ble_voice_probe::Kind::StartAck;
        _pending_start_ack.session = command.session;
        _pending_start_ack.sequence = _session.allocate_sequence();
        _pending_start_ack.watch_ms = now_ms;
        _pending_start_ack.payload_length = ble_voice_probe::encode_start_ack(
            negotiated_mtu,
            command.mode,
            command.target_bps,
            _pending_start_ack.payload.data(),
            _pending_start_ack.payload.size());
        if (_pending_start_ack.payload_length == 0) {
            reset_session_locked();
            return false;
        }
        _start_ack_pending = true;
        if (enqueue_packet(_pending_start_ack)) {
            _start_ack_queued = true;
        } else {
            _session.record_queue_overflow();
            ++_consecutive_failures;
        }
        _producer_enabled = true;
    }

    if (_producer_task != nullptr) {
        xTaskNotifyGive(static_cast<TaskHandle_t>(_producer_task));
    }
    return true;
}

void BleVoiceProbeController::request_stop(bool emit_summary, ble_voice_probe::ErrorCode reason)
{
    uint16_t session = 0;
    uint32_t sequence = 0;
    bool queue_error = false;
    {
        StateLock lock(_state_mutex);
        if (!lock.locked() || !_session.active()) {
            return;
        }

        _producer_enabled = false;
        _pending_data_valid = false;
        if (emit_summary) {
            _stop_requested = true;
        } else {
            session = _session.id();
            sequence = _session.allocate_sequence();
            reset_session_locked();
            if (_packet_queue != nullptr) {
                xQueueReset(static_cast<QueueHandle_t>(_packet_queue));
            }
            queue_error = true;
        }
    }

    if (_producer_task != nullptr) {
        xTaskNotifyGive(static_cast<TaskHandle_t>(_producer_task));
    }
    if (queue_error) {
        enqueue_error(reason, session, 0, GetHAL().millis(), sequence);
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
        if (!lock.locked() || !_session.active() || !_start_ack_pending || _start_ack_queued) {
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

void BleVoiceProbeController::finalize_stop(uint32_t now_ms)
{
    bool stop_for_congestion = false;
    {
        StateLock lock(_state_mutex);
        if (!lock.locked() || !_session.active() || !_stop_requested || _start_ack_pending ||
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
    _consecutive_failures = 0;
    _producer_enabled = false;
    _stop_requested = false;
    _start_ack_pending = false;
    _start_ack_queued = false;
    _summary_queued = false;
    _summary_prepared = false;
    _pending_data_valid = false;
    _pending_start_ack = {};
    _pending_data = {};
    _pending_summary = {};
}
