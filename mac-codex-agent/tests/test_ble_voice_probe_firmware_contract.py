import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class BleVoiceProbeFirmwareContractTests(unittest.TestCase):
    def test_gatt_has_isolated_transport_only_voice_characteristics(self):
        source = (ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp").read_text()
        probe = (ROOT / "mac-codex-agent/ble_voice_probe.py").read_text()
        docs = (ROOT / "docs/ble-voice-link-probe.md").read_text()
        self.assertIn("kVoiceServiceUuid", source)
        self.assertIn("kVoiceControlUuid", source)
        self.assertIn("kVoiceTxUuid", source)
        self.assertIn("BLE_GAP_EVENT_SUBSCRIBE", source)
        self.assertIn("BLE_GAP_EVENT_MTU", source)
        self.assertIn("ble_gatts_notify_custom", source)
        self.assertIn(
            'SERVICE_UUID = "7a230005-5474-4f50-5741-544348434f44"',
            probe,
        )
        services = source.split("const ble_gatt_svc_def kServices[]", 1)[1].split(
            "int start_advertising", 1
        )[0]
        self.assertIn("&kServiceUuid.u", services)
        self.assertIn("kStatusCharacteristics", services)
        self.assertIn("&kVoiceServiceUuid.u", services)
        self.assertIn("kVoiceCharacteristics", services)
        status_characteristics = source.split(
            "const ble_gatt_chr_def kStatusCharacteristics[]", 1
        )[1].split("const ble_gatt_chr_def kVoiceCharacteristics[]", 1)[0]
        self.assertIn("BLE_GATT_CHR_F_WRITE_ENC", status_characteristics)
        voice_characteristics = source.split(
            "const ble_gatt_chr_def kVoiceCharacteristics[]", 1
        )[1].split("const ble_gatt_svc_def kServices[]", 1)[0]
        self.assertIn(
            "BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP",
            voice_characteristics,
        )
        self.assertIn("BLE_GATT_CHR_F_NOTIFY", voice_characteristics)
        self.assertNotIn("BLE_GATT_CHR_F_WRITE_ENC", voice_characteristics)
        self.assertNotIn("BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC", voice_characteristics)
        advertising = source.split("int start_advertising()", 2)[2].split(
            "void on_reset", 1
        )[0]
        self.assertIn("&kVoiceServiceUuid", advertising)
        identity_setup = source.split("void on_sync()", 1)[1].split(
            "void host_task", 1
        )[0]
        self.assertIn("kVoiceProbeIdentityXor", source)
        self.assertIn(
            "identity_address[0] ^= kVoiceProbeIdentityXor",
            identity_setup,
        )
        voice_tx_definition = source.split("&kVoiceTxUuid.u", 1)[1].split("},", 1)[0]
        self.assertIn("voice_tx_access", voice_tx_definition)
        self.assertNotIn("nullptr,\n        nullptr,\n        nullptr,", voice_tx_definition)
        connect_event = source.split("case BLE_GAP_EVENT_CONNECT:", 1)[1].split(
            "case BLE_GAP_EVENT_DISCONNECT:", 1
        )[0]
        self.assertNotIn("ble_gap_security_initiate", connect_event)
        self.assertIn("transport-only", docs)
        self.assertIn("unencrypted", docs)

    def test_transport_bounds_control_queue_and_mtu_payload(self):
        header = (ROOT / "main/apps/app_codex_status/codex_ble_transport.h").read_text()
        source = (ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp").read_text()
        self.assertIn("take_voice_control", header)
        self.assertIn("voice_notification_limit", header)
        self.assertIn("kVoiceControlQueueDepth", source)
        self.assertIn("event->mtu.value", source)
        self.assertIn("negotiated_mtu - 3", source)

    def test_gap_security_outcomes_are_logged_for_physical_diagnosis(self):
        source = (ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp").read_text()
        self.assertIn("BLE_GAP_EVENT_ENC_CHANGE", source)
        self.assertIn("event->enc_change.status", source)
        self.assertIn("event->enc_change.conn_handle", source)
        self.assertIn("BLE_GAP_EVENT_PASSKEY_ACTION", source)
        self.assertIn("event->passkey.params.action", source)
        self.assertIn("event->passkey.conn_handle", source)

    def test_probe_security_uses_volatile_non_bonded_keys(self):
        source = (ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp").read_text()
        unified_defaults = (ROOT / "sdkconfig.unified.defaults").read_text()
        self.assertIn("ble_hs_cfg.sm_bonding = 0", source)
        self.assertIn("CONFIG_BT_NIMBLE_NVS_PERSIST=n", unified_defaults)
        self.assertNotIn("ble_store_ram_", source)

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

    def test_controller_shutdown_cancels_read_before_bounded_forced_deletion(self):
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
        self.assertNotIn("portMAX_DELAY", shutdown)
        self.assertNotIn("return;", shutdown)
        self.assertIn("xTaskAbortDelay", shutdown)
        self.assertIn("kProducerCancelGraceTimeout", shutdown)
        self.assertIn(
            "static_assert(kProducerCancelGraceTimeout > kCaptureReadTimeout",
            controller,
        )
        self.assertIn(
            "vTaskDelete(worker)", shutdown
        )
        self.assertIn("release_audio_resources(true)", shutdown)
        self.assertLess(shutdown.index("xTaskAbortDelay"), shutdown.index("kProducerCancelGraceTimeout"))
        self.assertLess(shutdown.index("kProducerCancelGraceTimeout"), shutdown.index("vTaskDelete"))
        self.assertLess(shutdown.index("vTaskDelete"), shutdown.index("release_audio_resources(true)"))
        self.assertIn("vTaskSuspend(nullptr)", controller)

    def test_audio_backend_read_holds_no_external_mutex_during_i2s_wait(self):
        backend = (ROOT / "main/hal/audio/stopwatch_audio_backend.cpp").read_text()
        read = backend.split("std::size_t StopWatchAudioBackend::read", 1)[1].split(
            "bool StopWatchAudioBackend::openPlayer", 1
        )[0]
        self.assertNotIn("recorder_io_mutex", read)
        self.assertIn("control_mutex", read)
        self.assertIn("i2s_channel_read", read)
        self.assertNotIn("timed_mutex", backend)

    def test_audio_backend_logs_recorder_open_failure_cause(self):
        backend = (ROOT / "main/hal/audio/stopwatch_audio_backend.cpp").read_text()
        open_recorder = backend.split(
            "bool StopWatchAudioBackend::openRecorder", 1
        )[1].split("void StopWatchAudioBackend::closeRecorder", 1)[0]
        self.assertIn("recorder open rejected", open_recorder)
        self.assertIn("recorder enable failed", open_recorder)
        self.assertIn("esp_err_to_name", open_recorder)

    def test_forced_audio_cleanup_never_takes_worker_state_mutex(self):
        controller = (
            ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.cpp"
        ).read_text()
        release = controller.split(
            "void BleVoiceProbeController::release_audio_resources", 1
        )[1].split("bool BleVoiceProbeController::start_session", 1)[0]
        forced_branch = release.split("if (forced_cleanup)", 1)[1].split(
            "StateLock lock", 1
        )[0]
        self.assertIn("_microphone_setup_pending = false", forced_branch)
        self.assertIn("_microphone_cleanup_pending = false", forced_branch)
        self.assertIn("return;", forced_branch)
        self.assertNotIn("StateLock", forced_branch)

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
        self.assertIn("recorder read failed", read)
        self.assertIn("esp_err_to_name", read)
        self.assertNotIn("esp_codec_dev_read", read)

    def test_terminal_error_replaces_rejected_packet_without_false_summary_success(self):
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
        self.assertIn("Kind::Data && _pending_data_queued", controller)
        terminal_error = controller.split(
            "void BleVoiceProbeController::prepare_terminal_error_locked", 1
        )[1].split("void BleVoiceProbeController::enqueue_error", 1)[0]
        self.assertIn(
            "_pending_data_valid && !_pending_data_queued", terminal_error
        )
        self.assertIn(
            "_start_ack_pending && !_start_ack_queued", terminal_error
        )
        self.assertIn(
            "_summary_prepared && !_summary_queued", terminal_error
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
        self.assertNotIn("retry_preserved_packet", controller)
        self.assertNotIn("ever_queued", controller)
        rejected_packet = controller.split(
            "void BleVoiceProbeController::replace_terminal_error_sequence_locked", 1
        )[1].split("void BleVoiceProbeController::enqueue_error", 1)[0]
        self.assertIn("Kind::StopSummary", rejected_packet)
        self.assertIn(
            "_pending_error.sequence = _pending_summary.sequence", rejected_packet
        )
        self.assertIn("_summary_prepared = false", rejected_packet)
        notify_result = controller.split(
            "void BleVoiceProbeController::record_notify_result", 1
        )[1].split("void BleVoiceProbeController::handle_disconnect", 1)[0]
        self.assertIn("replace_terminal_error_sequence_locked(kind)", notify_result)

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

    def test_offline_validation_and_probe_docs_cover_pcm_and_checkout_evidence(self):
        runner = (ROOT / "scripts/run_unified_offline_validation.sh").read_text()
        docs = (ROOT / "docs/ble-voice-link-probe.md").read_text()
        self.assertIn("[17/20] BLE voice probe PCM buffer host test", runner)
        self.assertIn("tests/ble_voice_probe_pcm_buffer_test.cpp", runner)
        self.assertIn("[20/20] Whitespace error check", runner)
        self.assertLess(
            runner.index("tests/ble_voice_probe_pcm_buffer_test.cpp"),
            runner.index("./scripts/build_unified_candidate.sh --clean"),
        )
        self.assertNotIn("/Users/bytedance/Projects.localized/Codex/StopWatch", docs)
        self.assertIn('ROOT="$(git rev-parse --show-toplevel)"', docs)
        self.assertIn('REVISION="$(git -C "$ROOT" rev-parse HEAD)"', docs)
        self.assertIn('FIRMWARE="${FIRMWARE:-$ROOT/build-unified/StopWatch-UserDemo.bin}"', docs)
        self.assertIn('shasum -a 256 "$FIRMWARE"', docs)
        self.assertIn("firmware-app.sha256", docs)


if __name__ == "__main__":
    unittest.main()
