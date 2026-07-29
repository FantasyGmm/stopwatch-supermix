#pragma once

#include "ble_voice_probe_protocol.h"
#include "ble_voice_probe_session.h"
#include "codex_ble_transport.h"
#include "voice_capture_pipeline.h"

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>

class BleVoiceProbeController {
public:
    BleVoiceProbeController();
    ~BleVoiceProbeController();

    bool initialize();
    void handle_control(const CodexBleVoiceControl& frame, bool subscribed,
                        uint16_t negotiated_mtu, uint32_t now_ms);
    void poll(uint32_t now_ms);
    bool take_notification(ble_voice_probe::Notification& packet);
    void record_notify_result(ble_voice_probe::Kind kind,
                              CodexBleNotifyResult result, uint32_t now_ms);
    void handle_disconnect();
    void shutdown();

private:
    static void producer_entry(void* context);
    void producer_loop();
    bool start_session(const ble_voice_probe::ControlCommand& command,
                       uint16_t negotiated_mtu, uint32_t now_ms);
    void request_stop(bool emit_summary, ble_voice_probe::ErrorCode reason);
    bool enqueue_packet(ble_voice_probe::Notification& packet);
    bool queue_start_ack_locked(uint16_t negotiated_mtu,
                                ble_voice_probe::Mode mode,
                                uint32_t target_bps,
                                uint32_t now_ms);
    bool prepare_synthetic_packet_locked(uint32_t now_ms);
    bool prepare_microphone_packet(
        const std::array<uint8_t, VoiceCapturePipeline::kAdpcmBytesPerBlock>& encoded,
        uint32_t captured_samples,
        ble_voice_probe::ErrorCode& error);
    // Called without _state_mutex; this helper owns failure-transition locking.
    void transition_microphone_failure(ble_voice_probe::ErrorCode error);
    void release_audio_resources(bool forced_cleanup = false);
    void retry_start_ack();
    void retry_terminal_error();
    void finalize_stop(uint32_t now_ms);
    bool prepare_crypto_failure_summary_locked(uint32_t now_ms);
    void prepare_terminal_error_locked(ble_voice_probe::ErrorCode code,
                                       uint16_t detail, uint32_t now_ms);
    void replace_terminal_error_sequence_locked(ble_voice_probe::Kind kind);

    void enqueue_error(ble_voice_probe::ErrorCode code, uint16_t session,
                       uint16_t detail, uint32_t now_ms, uint32_t sequence = 0);
    uint16_t session_for_error(const CodexBleVoiceControl& frame) const;
    uint16_t notification_limit(uint16_t negotiated_mtu) const;
    void reset_session_locked();

    void* _packet_queue = nullptr;
    void* _state_mutex = nullptr;
    void* _producer_stopped = nullptr;
    // The controller is the only owner allowed to delete the producer task.
    void* _producer_task = nullptr;
    VoiceCapturePipeline _capture_pipeline;
    ble_voice_probe::Session _session;
    ble_voice_probe::Notification _pending_start_ack = {};
    ble_voice_probe::Notification _pending_data = {};
    ble_voice_probe::Notification _pending_summary = {};
    ble_voice_probe::Notification _pending_error = {};
    std::atomic<bool> _shutdown_requested{false};
    uint16_t _notification_payload_capacity = 0;
    uint16_t _negotiated_mtu = 0;
    uint32_t _pending_captured_samples = 0;
    uint8_t _consecutive_failures = 0;
    bool _producer_enabled = false;
    bool _stop_requested = false;
    bool _start_ack_pending = false;
    bool _start_ack_queued = false;
    bool _summary_queued = false;
    bool _summary_prepared = false;
    bool _pending_data_valid = false;
    bool _pending_data_queued = false;
    bool _pending_data_counted = false;
    bool _terminal_error_pending = false;
    bool _terminal_error_queued = false;
    bool _microphone_setup_pending = false;
    bool _microphone_cleanup_pending = false;
    bool _link_active = false;
    bool _initialized = false;
};
