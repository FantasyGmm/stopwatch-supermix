import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class BleVoiceProbeFirmwareContractTests(unittest.TestCase):
    def test_gatt_has_isolated_encrypted_voice_characteristics(self):
        source = (ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp").read_text()
        self.assertIn("kVoiceControlUuid", source)
        self.assertIn("kVoiceTxUuid", source)
        self.assertIn("BLE_GATT_CHR_F_WRITE_ENC", source)
        self.assertIn("BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC", source)
        self.assertIn("BLE_GAP_EVENT_SUBSCRIBE", source)
        self.assertIn("BLE_GAP_EVENT_MTU", source)
        self.assertIn("ble_gatts_notify_custom", source)

    def test_transport_bounds_control_queue_and_mtu_payload(self):
        header = (ROOT / "main/apps/app_codex_status/codex_ble_transport.h").read_text()
        source = (ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp").read_text()
        self.assertIn("take_voice_control", header)
        self.assertIn("voice_notification_limit", header)
        self.assertIn("kVoiceControlQueueDepth", source)
        self.assertIn("event->mtu.value", source)
        self.assertIn("negotiated_mtu - 3", source)

    def test_controller_is_bounded_and_not_attached_to_a_button(self):
        controller = (
            ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.cpp"
        ).read_text()
        app = (ROOT / "main/apps/app_codex_status/app_codex_status.cpp").read_text()
        self.assertIn("xQueueCreate(kVoicePacketQueueDepth", controller)
        self.assertIn("kMaxConsecutiveQueueFailures", controller)
        self.assertIn("fill_synthetic_payload", controller)
        self.assertIn("retry_start_ack", controller)
        self.assertIn("_start_ack_pending", controller)
        self.assertIn("_summary_prepared", controller)
        self.assertIn("take_voice_control", app)
        self.assertIn("notify_voice", app)
        self.assertIn("handle_disconnect", app)
        self.assertIn("shutdown", app)
        self.assertNotIn("Typeless", controller)
        self.assertNotIn("KeyEvent::Right", controller)
        self.assertNotIn("start_session(key_event", app)

    def test_controller_shutdown_waits_past_capture_timeout_before_worker_deletion(self):
        controller = (
            ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.cpp"
        ).read_text()
        shutdown = controller.split("void BleVoiceProbeController::shutdown()", 1)[1].split(
            "void BleVoiceProbeController::producer_entry", 1
        )[0]
        self.assertIn("kProducerStopTimeout", shutdown)
        self.assertIn(
            "kCaptureReadTimeout = pdMS_TO_TICKS(100)", controller
        )
        self.assertIn(
            "kProducerStopTimeout = pdMS_TO_TICKS(500)", controller
        )
        self.assertIn(
            "static_assert(kProducerStopTimeout > kCaptureReadTimeout", controller
        )
        self.assertIn(
            "xSemaphoreTake(static_cast<SemaphoreHandle_t>(_producer_stopped), portMAX_DELAY)",
            shutdown,
        )
        self.assertIn(
            "vTaskDelete(static_cast<TaskHandle_t>(_producer_task))", shutdown
        )
        self.assertLess(shutdown.index("portMAX_DELAY"), shutdown.index("vTaskDelete"))
        self.assertIn("vTaskSuspend(nullptr)", controller)

    def test_audio_backend_read_uses_caller_timeout_on_owned_rx_channel(self):
        backend = (ROOT / "main/hal/audio/stopwatch_audio_backend.cpp").read_text()
        read = backend.split("std::size_t StopWatchAudioBackend::read", 1)[1].split(
            "bool StopWatchAudioBackend::openPlayer", 1
        )[0]
        self.assertNotIn("(void)timeout", read)
        self.assertIn("i2s_channel_read(", read)
        self.assertIn("_impl->rx_handle", read)
        self.assertIn("timeout_ms", read)
        self.assertIn("timeout == portMAX_DELAY", read)
        self.assertIn("UINT32_MAX", read)
        self.assertIn("bytes_read", read)
        self.assertNotIn("esp_codec_dev_read", read)

    def test_terminal_error_preserves_packet_sequence_and_queue_order(self):
        controller = (
            ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.cpp"
        ).read_text()
        request_stop = controller.split(
            "void BleVoiceProbeController::request_stop", 1
        )[1].split("bool BleVoiceProbeController::enqueue_packet", 1)[0]
        microphone_failure = controller.split(
            "void BleVoiceProbeController::fail_microphone_session", 1
        )[1].split("void BleVoiceProbeController::release_audio_resources", 1)[0]
        self.assertNotIn("xQueueReset", request_stop)
        self.assertNotIn("xQueueReset", microphone_failure)
        self.assertIn("prepare_terminal_error_locked", request_stop)
        self.assertIn("prepare_terminal_error_locked", microphone_failure)
        self.assertIn("retry_terminal_error", controller)
        self.assertIn("_pending_data_queued", controller)
        self.assertIn("_pending_data_ever_queued", controller)
        self.assertIn("Kind::Data && _pending_data_queued", controller)
        terminal_error = controller.split(
            "void BleVoiceProbeController::prepare_terminal_error_locked", 1
        )[1].split("void BleVoiceProbeController::enqueue_error", 1)[0]
        self.assertIn(
            "_pending_data_valid && !_pending_data_ever_queued", terminal_error
        )
        self.assertIn(
            "_start_ack_pending && !_start_ack_ever_queued", terminal_error
        )
        self.assertIn(
            "_summary_prepared && !_summary_ever_queued", terminal_error
        )
        retry_error = controller.split(
            "void BleVoiceProbeController::retry_terminal_error", 1
        )[1].split("void BleVoiceProbeController::finalize_stop", 1)[0]
        for tracked_state in (
            "_start_ack_pending",
            "_start_ack_queued",
            "_pending_data_valid",
            "_pending_data_queued",
            "_summary_prepared",
            "_summary_queued",
        ):
            self.assertIn(tracked_state, retry_error)
        self.assertIn("retry_preserved_packet", controller)

    def test_microphone_producer_drains_complete_pcm_before_more_reads(self):
        controller = (
            ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.cpp"
        ).read_text()
        loop = controller.split("void BleVoiceProbeController::producer_loop", 1)[1].split(
            "bool BleVoiceProbeController::queue_start_ack_locked", 1
        )[0]
        self.assertIn("_microphone_pcm.has_complete_block()", loop)
        self.assertIn("prepare_microphone_packet", loop)
        self.assertLess(
            loop.index("_microphone_pcm.has_complete_block()"),
            loop.index("capture_microphone = true"),
        )

    def test_microphone_resources_are_acquired_and_released(self):
        controller = (
            ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.cpp"
        ).read_text()
        self.assertIn("acquireCapture(RuntimeOwner::Codex)", controller)
        self.assertIn("releaseCapture(RuntimeOwner::Codex)", controller)
        self.assertIn("openRecorder(44100, 16, 1)", controller)
        self.assertIn("closeRecorder()", controller)
        self.assertIn("esp_ae_rate_cvt_open", controller)
        self.assertIn("esp_ae_rate_cvt_close", controller)
        self.assertIn("encode_ima_adpcm_block", controller)
        self.assertIn("record_captured_samples", controller)


if __name__ == "__main__":
    unittest.main()
