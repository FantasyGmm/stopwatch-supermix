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
        self.assertNotIn("BLE_GATT_CHR_F_WRITE_ENC", status_characteristics)
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
        self.assertNotIn("kVoiceProbeIdentityXor", source)
        self.assertIn("ble_hs_util_ensure_addr(1)", identity_setup)
        self.assertIn("ble_hs_id_infer_auto(0", identity_setup)
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

    def test_pending_pairing_timeout_tracks_activity_with_a_hard_cap(self):
        source = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp"
        ).read_text()
        self.assertIn("kPendingAuthenticationIdleTimeoutMs", source)
        self.assertIn("kPendingAuthenticationMaxLifetimeMs", source)
        pairing_enqueue = source.split(
            "bool CodexBleTransport::enqueue_pairing_frame_from_ble", 1
        )[1].split("bool CodexBleTransport::handle_gap_connected", 1)[0]
        self.assertNotIn("note_pending_activity", pairing_enqueue)
        self.assertIn("note_pairing_activity", source)
        app_source = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        running = app_source.split("void AppCodexStatus::onRunning", 1)[1].split(
            "void AppCodexStatus::onClose", 1
        )[0]
        self.assertLess(
            running.index("process_secure_pairing(now)"),
            running.index("_ble_transport->poll(now)"),
        )
        pairing = app_source.split(
            "void AppCodexStatus::process_secure_pairing", 1
        )[1].split("void AppCodexStatus::complete_agent_switch_if_ready", 1)[0]
        self.assertIn("note_pairing_activity", pairing)

    def test_rejected_pending_disconnect_is_retried_until_gap_confirms(self):
        source = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp"
        ).read_text()
        poll = source.split("void CodexBleTransport::poll", 1)[1].split(
            "bool CodexBleTransport::connected", 1
        )[0]
        disconnect = source.split(
            "void CodexBleTransport::handle_gap_disconnected", 1
        )[1].split("void CodexBleTransport::handle_subscription", 1)[0]
        self.assertIn("disconnecting_handle", poll)
        self.assertIn("ble_gap_terminate", poll)
        self.assertIn("AgentConnectionRole::Disconnecting", disconnect)

    def test_rejected_new_connection_disconnect_is_retried_until_gap_confirms(self):
        header = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.h"
        ).read_text()
        source = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp"
        ).read_text()
        poll = source.split("void CodexBleTransport::poll", 1)[1].split(
            "bool CodexBleTransport::connected", 1
        )[0]
        connected = source.split(
            "bool CodexBleTransport::handle_gap_connected", 1
        )[1].split("void CodexBleTransport::handle_gap_disconnected", 1)[0]
        disconnected = source.split(
            "void CodexBleTransport::handle_gap_disconnected", 1
        )[1].split("void CodexBleTransport::handle_subscription", 1)[0]
        self.assertIn("_rejected_disconnect_handle", header)
        self.assertIn("_rejected_disconnect_mutex", header)
        self.assertIn("_rejected_disconnect_handle", poll)
        self.assertIn("lock_guard<std::mutex>", poll)
        self.assertIn("ble_gap_terminate", poll)
        self.assertIn("AgentConnectionAdmission::RejectedFull", connected)
        self.assertIn(
            "_rejected_disconnect_handle = connection_handle", connected
        )
        self.assertIn("lock_guard<std::mutex>", disconnected)
        self.assertIn("_rejected_disconnect_handle", disconnected)

    def test_active_agent_can_rekey_without_losing_production_access(self):
        router = (
            ROOT / "main/apps/app_codex_status/agent_connection_router.cpp"
        ).read_text()
        transport_header = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.h"
        ).read_text()
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        runtime = (ROOT / "mac-codex-agent/runtime.py").read_text()
        self.assertIn("AgentConnectionRole::Active", router.split(
            "surface == AgentGattSurface::Pairing", 1
        )[1].split("bool AgentConnectionRouter::update_mtu", 1)[0])
        self.assertIn("is_active_handle", transport_header)
        pairing = app.split(
            "void AppCodexStatus::process_secure_pairing", 1
        )[1].split("void AppCodexStatus::complete_agent_switch_if_ready", 1)[0]
        self.assertIn("is_active_handle", pairing)
        self.assertIn("refresh_if_needed", runtime)
        self.assertIn("_refresh_secure_credential", runtime)
        self.assertNotIn(
            "connection->pairing_subscribed = false",
            router.split("bool AgentConnectionRouter::promote_pending", 1)[1]
            .split("bool AgentConnectionRouter::reject_pending", 1)[0],
        )

        for message_type in (
            "PairingConfirm",
            "AuthenticationProof",
            "CredentialAck",
        ):
            handler = pairing.split(
                f"WireMessageType::{message_type}", 1
            )[1].split("continue;", 1)[0]
            self.assertIn("connection_handle()", handler)
            self.assertIn("queued.connection_handle", handler)

    def test_rekey_disconnect_keeps_status_available_unless_identity_revoked(self):
        header = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.h"
        ).read_text()
        source = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp"
        ).read_text()
        self.assertIn("request_active_disconnect(bool revoke_production", header)
        self.assertIn("_active_production_revoked_token", header)
        authorization = source.split(
            "bool CodexBleTransport::authorize_gatt", 1
        )[1].split("bool CodexBleTransport::authorize_pending", 1)[0]
        self.assertIn("_active_production_revoked_token.load()", authorization)
        self.assertNotIn("_active_disconnect_token.load()", authorization)

    def test_revoked_active_cannot_receive_outbound_voice(self):
        header = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.h"
        ).read_text()
        source = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp"
        ).read_text()
        send = source.split(
            "VoiceNotificationSendResult CodexBleTransport::send_voice_notification",
            1,
        )[1].split(
            "CodexBleNotifyResult CodexBleTransport::notify_voice_for_link", 1
        )[0]
        self.assertIn("_voice_link_mutex", header)
        self.assertIn("_voice_link_mutex", send)
        self.assertIn("_active_production_revoked_token.load()", send)
        self.assertIn("notification.connection_generation", send)
        self.assertIn("link.generation", send)

    def test_forced_capture_worker_failure_cannot_duplicate_transmitter(self):
        header = (
            ROOT / "main/apps/app_codex_status/voice_input_controller.h"
        ).read_text()
        source = (
            ROOT / "main/apps/app_codex_status/voice_input_controller.cpp"
        ).read_text()
        stop_capture = source.split(
            "void VoiceInputController::stop_capture_bounded", 1
        )[1].split("bool VoiceInputController::wait_transmitter_idle_bounded", 1)[0]

        self.assertIn("_producer_worker_failed", header)
        self.assertIn("_producer_worker_failed = true", stop_capture)
        self.assertNotIn("_initialized = false", stop_capture)
        self.assertNotIn("vTaskDelete", stop_capture)
        self.assertIn("esp_restart()", stop_capture)

    def test_voice_start_state_is_published_before_transmitter_wakeup(self):
        source = (
            ROOT / "main/apps/app_codex_status/voice_input_controller.cpp"
        ).read_text()
        start = source.split("bool VoiceInputController::start_session", 1)[1].split(
            "bool VoiceInputController::prepare_begin", 1
        )[0]
        prepare_begin = source.split(
            "bool VoiceInputController::prepare_begin", 1
        )[1].split("bool VoiceInputController::prepare_audio", 1)[0]

        self.assertNotIn("wake_transmitter", prepare_begin)
        self.assertLess(start.index("_producer_running = true"), start.index("wake_transmitter()"))
        self.assertLess(
            start.index("_ui_state = VoiceInputUiState::Active"),
            start.index("wake_transmitter()"),
        )

    def test_voice_worker_timeout_uses_fail_stop_not_forced_task_delete(self):
        source = (
            ROOT / "main/apps/app_codex_status/voice_input_controller.cpp"
        ).read_text()
        wait_tx = source.split(
            "bool VoiceInputController::wait_transmitter_idle_bounded", 1
        )[1].split("void VoiceInputController::stop_transmitter_bounded", 1)[0]

        self.assertNotIn("vTaskDelete", wait_tx)
        self.assertIn("esp_restart()", wait_tx)

    def test_generation_bound_disconnect_holds_voice_link_lock_through_terminate(self):
        source = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp"
        ).read_text()
        request = source.split(
            "bool CodexBleTransport::request_active_disconnect", 1
        )[1].split("bool CodexBleTransport::is_active_handle", 1)[0]
        poll = source.split("void CodexBleTransport::poll", 1)[1].split(
            "bool CodexBleTransport::connected", 1
        )[0]

        reject = source.split(
            "bool CodexBleTransport::reject_pending", 1
        )[1].split("bool CodexBleTransport::request_active_disconnect", 1)[0]

        self.assertIn("serialize_connection_termination", request)
        self.assertIn("serialize_connection_termination", reject)
        self.assertGreaterEqual(
            poll.count("serialize_connection_termination"), 2
        )
        self.assertIn("_voice_link_mutex", request)
        self.assertIn("_voice_link_mutex", reject)
        self.assertIn("ble_gap_terminate", request)
        self.assertIn("ble_gap_terminate", reject)

    def test_disconnect_retry_is_bound_to_original_active_generation(self):
        header = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.h"
        ).read_text()
        source = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp"
        ).read_text()
        poll = source.split("void CodexBleTransport::poll", 1)[1].split(
            "bool CodexBleTransport::connected", 1
        )[0]
        request = source.split(
            "bool CodexBleTransport::request_active_disconnect", 1
        )[1].split("uint16_t CodexBleTransport::pending_handle", 1)[0]
        self.assertIn("_active_disconnect_token", header)
        self.assertIn("active_connection_token", request)
        self.assertIn("active_connection_token", poll)
        self.assertNotIn("active_handle()", poll.split("_active_disconnect_token", 1)[1])

    def test_production_voice_control_is_authenticated_and_separate_from_probe(self):
        header = (ROOT / "main/apps/app_codex_status/codex_ble_transport.h").read_text()
        source = (ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp").read_text()
        app_header = (ROOT / "main/apps/app_codex_status/app_codex_status.h").read_text()
        self.assertIn("take_production_voice_control", header)
        self.assertIn("voice_input::kProtocolVersion", source)
        self.assertIn("ControlReplayGuard", header)
        self.assertIn("decode_once", source)
        self.assertIn("_production_voice_control_queue", header)
        self.assertIn("_voice_control_queue", header)
        self.assertIn("connection_epoch", header)
        self.assertIn("_production_voice_connection_epoch", app_header)
        disconnect = source.split("void CodexBleTransport::handle_gap_disconnected", 1)[1]
        self.assertIn("_production_control_guard.reset()", disconnect)
        self.assertIn("_connection_router.disconnected", disconnect)
        self.assertIn("active_generation", source)

    def test_production_voice_uses_runtime_key_and_strict_epoch(self):
        transport_header = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.h"
        ).read_text()
        transport_source = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp"
        ).read_text()
        controller_header = (
            ROOT / "main/apps/app_codex_status/voice_input_controller.h"
        ).read_text()
        controller_source = (
            ROOT / "main/apps/app_codex_status/voice_input_controller.cpp"
        ).read_text()
        crypto_header = (
            ROOT / "main/apps/app_codex_status/voice_input_crypto.h"
        ).read_text()
        protocol_header = (
            ROOT / "main/apps/app_codex_status/voice_input_protocol.h"
        ).read_text()

        self.assertIn("kProtocolVersion = 5", protocol_header)
        self.assertIn("uint32_t key_epoch", protocol_header)
        self.assertIn("uint32_t expected_key_epoch", protocol_header)
        self.assertIn("set_voice_credential", transport_header)
        self.assertIn("clear_voice_credential", transport_header)
        self.assertIn("_production_voice_key_epoch", transport_header)
        self.assertIn("_production_voice_key_epoch.load", transport_source)
        self.assertIn("set_credential", controller_header)
        self.assertIn("clear_credential", controller_header)
        self.assertIn("_key_epoch", controller_header)
        self.assertIn("_crypto->set_key", controller_source)
        self.assertIn("_crypto->clear", controller_source)
        self.assertIn("bool set_key", crypto_header)
        self.assertIn("void clear", crypto_header)

        production = "\n".join(
            (transport_header, transport_source, controller_header, controller_source)
        )
        self.assertNotIn("device_context", production)
        self.assertNotIn("generated::kDeviceKey", production)

    def test_input_actions_have_independent_authenticated_gatt_path(self):
        header = (ROOT / "main/apps/app_codex_status/codex_ble_transport.h").read_text()
        source = (ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp").read_text()
        controller = (
            ROOT / "main/apps/app_codex_status/input_action_controller.cpp"
        ).read_text()
        app_header = (ROOT / "main/apps/app_codex_status/app_codex_status.h").read_text()
        app = (ROOT / "main/apps/app_codex_status/app_codex_status.cpp").read_text()

        self.assertIn("kInputControlUuid", source)
        self.assertIn("kInputTxUuid", source)
        self.assertIn("0x09, 0x00, 0x23, 0x7a", source)
        self.assertIn("0x0a, 0x00, 0x23, 0x7a", source)
        self.assertIn("take_input_action_control", header)
        self.assertIn("input_action_subscribed", header)
        self.assertIn("notify_input_action", header)
        self.assertIn("_input_control_queue", header)
        self.assertIn("_input_action_subscribed", header)
        self.assertIn("_input_tx_value_handle", header)
        self.assertIn("decode_ready", controller)
        self.assertIn("_input_control_guard", header)

        disconnect = source.split(
            "void CodexBleTransport::handle_gap_disconnected", 1
        )[1]
        self.assertIn("_input_control_guard.reset()", disconnect)
        self.assertIn("_input_action_subscribed = false", disconnect)
        self.assertIn("_input_tx_value_handle = 0", disconnect)

        self.assertIn("public CodexButtonActionSink", app_header)
        self.assertIn("InputActionController", app_header)
        running = app.split("void AppCodexStatus::onRunning()", 1)[1].split(
            "void AppCodexStatus::onClose()", 1
        )[0]
        self.assertIn("take_input_action_control", running)
        self.assertIn("notify_input_action", running)
        self.assertLess(
            running.index("take_input_action_control"),
            running.index("notify_input_action"),
        )
        epoch_reset = running.split(
            "connection_epoch != _production_voice_connection_epoch", 1
        )[1].split("CodexInputActionControl", 1)[0]
        self.assertIn("_input_action_controller->disconnect()", epoch_reset)
        self.assertIn("_voice_button_dispatcher.reset(right_pressed)", epoch_reset)

    def test_app_routes_button_edges_after_wake_and_cancels_before_home(self):
        header = (ROOT / "main/apps/app_codex_status/app_codex_status.h").read_text()
        source = (ROOT / "main/apps/app_codex_status/app_codex_status.cpp").read_text()
        self.assertIn("VoiceInputController", header)
        self.assertIn("std::atomic<VoiceInputUiState>", header)
        self.assertIn("VoiceButtonDispatcher", header)
        self.assertIn("btnA.isPressed()", source)
        self.assertIn("btnB.isPressed()", source)
        controller_header = (
            ROOT / "main/apps/app_codex_status/voice_input_controller.h"
        ).read_text()
        self.assertIn("kChordGuardMs = 90", controller_header)
        self.assertIn("kVoiceHoldMs = 300", controller_header)
        self.assertIn("CancelVoice", controller_header)
        opened = source.split("void AppCodexStatus::onOpen()", 1)[1].split(
            "void AppCodexStatus::onRunning()", 1
        )[0]
        self.assertIn("updateButtonStates", opened)
        self.assertLess(
            opened.index("updateButtonStates"),
            opened.index("_voice_button_dispatcher.reset"),
        )
        running = source.split("void AppCodexStatus::onRunning()", 1)[1].split(
            "void AppCodexStatus::onClose()", 1
        )[0]
        self.assertLess(
            running.index("updateButtonStates"),
            running.index("update_screen_off_control"),
        )
        self.assertLess(
            running.index("update_screen_off_control"),
            running.index("btnA.isPressed()"),
        )
        self.assertLess(
            running.index("btnA.isPressed()"), running.index("btnB.isPressed()")
        )
        self.assertLess(
            running.index("btnB.isPressed()"),
            running.index("_key_manager->update(false)"),
        )
        self.assertIn("_key_manager->update(false)", running)
        self.assertLess(
            running.index("_key_manager->update(false)"),
            running.index("_voice_button_dispatcher.dispatch"),
        )
        self.assertEqual(running.count("updateButtonStates"), 1)
        self.assertEqual(running.count("_voice_button_dispatcher.dispatch"), 1)
        self.assertIn("GetHAL().buttonInputSuppressed()", running)
        self.assertNotIn("VoiceButtonAction::", running)
        self.assertNotIn("->handle_button", running)
        self.assertNotIn("->cancel_for_home", running)
        self.assertIn("sync_display_protection_presentation()", running)
        self.assertIn("update_page_swipe(now)", running)
        self.assertIn("_voice_status.store", running)
        self.assertLess(
            running.index("_voice_button_dispatcher.dispatch"),
            running.index("close();"),
        )
        self.assertLess(
            running.index("close();"),
            running.index("sync_display_protection_presentation()"),
        )
        priority = (
            ROOT / "main/apps/app_codex_status/codex_status_priority.h"
        ).read_text()
        for label in ("VOICE", "VOICE OFF", "VOICE ERR"):
            self.assertIn(f'"{label}"', priority)

    def test_pinned_microphone_page_uses_approved_three_page_layout(self):
        header = (ROOT / "main/apps/app_codex_status/app_codex_status.h").read_text()
        source = (ROOT / "main/apps/app_codex_status/app_codex_status.cpp").read_text()

        self.assertIn("PinnedMicrophoneController", header)
        self.assertIn("_pinned_microphone_page", header)
        self.assertIn("_page_dot_pinned_microphone", header)
        self.assertIn("_pinned_microphone_touch_target", header)
        self.assertNotIn("_pinned_microphone_start_ring", header)
        self.assertNotIn("_pinned_microphone_wave_inner_left", header)
        self.assertNotIn("_pinned_microphone_wave_outer_right", header)
        self.assertIn("_pinned_microphone_connection_label", header)
        self.assertIn("_pinned_microphone_prompt_label", header)
        self.assertIn("_pinned_microphone_capsule", header)
        self.assertIn("_pinned_microphone_cup", header)
        self.assertIn("_pinned_microphone_stem", header)
        self.assertIn("_pinned_microphone_base", header)
        self.assertIn("kPageCount                 = 3", source)
        self.assertIn("lv_obj_set_size(_page_strip, 1398, 466)", source)
        self.assertIn("lv_obj_set_pos(_pinned_microphone_page, 0, 0)", source)
        self.assertIn("lv_obj_set_pos(_codex_page, 466, 0)", source)
        self.assertIn("lv_obj_set_pos(_trae_page_root, 932, 0)", source)
        self.assertIn('"固定麦克风"', source)
        self.assertIn('"已连接到 Mac"', source)
        self.assertIn('"轻触开启"', source)
        self.assertIn('"正在启动音频…"', source)
        self.assertIn('"轻触关闭"', source)
        self.assertIn('"正在关闭…"', source)
        self.assertIn('"启动失败 · 轻触重试"', source)
        self.assertNotIn('"收音中"', source)
        self.assertNotIn('"MAC · EXTERNAL MIC"', source)
        self.assertIn("kQuotaMint", source)
        self.assertIn("kBeaconViolet", source)
        self.assertIn("kRunningCyan", source)
        self.assertIn("pinned_microphone_touch_event_cb", source)
        self.assertIn("LV_EVENT_CLICKED", source)
        self.assertIn(
            "lv_obj_set_style_border_width(\n"
            "        _pinned_microphone_touch_target, 0, 0)",
            source,
        )
        self.assertIn(
            "lv_obj_set_style_bg_opa(\n"
            "        _pinned_microphone_touch_target, LV_OPA_TRANSP, 0)",
            source,
        )
        pinned_render = source.split(
            "void AppCodexStatus::render_pinned_microphone", 1
        )[1].split(
            "void AppCodexStatus::pinned_microphone_touch_event_cb", 1
        )[0]
        self.assertNotIn("lv_obj_set_style_shadow", pinned_render)
        self.assertNotIn("_pinned_microphone_wave_", source)
        self.assertNotIn("_pinned_microphone_start_ring", source)
        pinned_callback = source.split(
            "void AppCodexStatus::pinned_microphone_touch_event_cb", 1
        )[1].split("void AppCodexStatus::update_battery", 1)[0]
        self.assertIn("_task_controller_mutex", pinned_callback)
        self.assertLess(
            pinned_callback.index("_task_controller_mutex"),
            pinned_callback.index("set_enabled("),
        )
        self.assertNotIn("lv_switch_create(_pinned_microphone_page)", source)
        pager = source.split("void AppCodexStatus::update_page_indicator", 1)[1].split(
            "void AppCodexStatus::page_animation_cb", 1
        )[0]
        self.assertNotIn("18, 5", pager)
        self.assertNotIn("432", pager)
        self.assertNotIn("_battery_label", pager)
        self.assertIn("codex_active ? LV_OPA_COVER : LV_OPA_TRANSP", pager)
        self.assertIn("pinned_active ? LV_OPA_COVER : LV_OPA_TRANSP", pager)
        self.assertIn("trae_active ? LV_OPA_COVER : LV_OPA_TRANSP", pager)
        panel_sync = source.split(
            "bool AppCodexStatus::sync_agent_panel_for_task_mode", 1
        )[1].split("void AppCodexStatus::sync_task_focus_visibility", 1)[0]
        self.assertIn("_current_page != 1", panel_sync)
        self.assertIn("PinnedMicTitle24", source)
        self.assertIn("PinnedMicState22", source)
        self.assertNotIn("LV_SYMBOL_AUDIO", source)

    def test_production_controller_has_bounded_task_and_encrypted_queue(self):
        header = (ROOT / "main/apps/app_codex_status/voice_input_controller.h").read_text()
        source = (ROOT / "main/apps/app_codex_status/voice_input_controller.cpp").read_text()
        self.assertIn("VoiceCapturePipeline", header)
        self.assertIn("VoiceInputSession", header)
        self.assertIn("kProducerStackBytes = 8192", source)
        self.assertIn("kMaxBufferedAudioMs = 1000", source)
        self.assertIn("encode_notification", source)
        self.assertIn("take_notification", source)
        self.assertIn("cancel_for_home", source)

    def test_production_voice_delivery_is_not_driven_by_codex_ui_loop(self):
        app = (ROOT / "main/apps/app_codex_status/app_codex_status.cpp").read_text()
        controller = (
            ROOT / "main/apps/app_codex_status/voice_input_controller.cpp"
        ).read_text()
        running = app.split("void AppCodexStatus::onRunning()", 1)[1].split(
            "void AppCodexStatus::onClose()", 1
        )[0]
        production_voice = running.split("if (_voice_input)", 1)[1].split(
            "if (_voice_probe)", 1
        )[0]

        self.assertNotIn("take_notification", production_voice)
        self.assertNotIn("record_notification_result", production_voice)
        self.assertNotIn("notify_voice(", production_voice)
        self.assertIn("transmitter_loop", controller)
        self.assertIn("xTaskCreate", controller)

    def test_capture_completion_is_published_before_transmitter_wakeup(self):
        controller = (
            ROOT / "main/apps/app_codex_status/voice_input_controller.cpp"
        ).read_text()
        producer = controller.split(
            "void VoiceInputController::producer_step", 1
        )[1].split("bool VoiceInputController::start_session", 1)[0]

        self.assertIn(
            "active_guard.completed = true;\n"
            "    _producer_running = false;\n"
            "    wake_transmitter();",
            producer,
        )

    def test_gap_security_outcomes_are_logged_for_physical_diagnosis(self):
        source = (ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp").read_text()
        self.assertIn("BLE_GAP_EVENT_ENC_CHANGE", source)
        self.assertIn("event->enc_change.status", source)
        self.assertIn("event->enc_change.conn_handle", source)
        self.assertIn("BLE_GAP_EVENT_PASSKEY_ACTION", source)
        self.assertIn("event->passkey.params.action", source)
        self.assertIn("event->passkey.conn_handle", source)

    def test_production_transport_does_not_require_system_pairing(self):
        source = (ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp").read_text()
        unified_defaults = (ROOT / "sdkconfig.unified.defaults").read_text()
        status_characteristics = source.split(
            "const ble_gatt_chr_def kStatusCharacteristics[]", 1
        )[1].split("const ble_gatt_chr_def kVoiceCharacteristics[]", 1)[0]
        connect_case = source.split("case BLE_GAP_EVENT_CONNECT:", 1)[1].split(
            "case BLE_GAP_EVENT_DISCONNECT:", 1
        )[0]

        self.assertNotIn("BLE_GATT_CHR_F_WRITE_ENC", status_characteristics)
        self.assertNotIn("ble_gap_security_initiate", connect_case)
        self.assertIn("ble_hs_cfg.sm_bonding = 0", source)
        self.assertIn("ble_hs_cfg.sm_our_key_dist = 0", source)
        self.assertIn("ble_hs_cfg.sm_their_key_dist = 0", source)
        self.assertIn("CONFIG_BT_NIMBLE_NVS_PERSIST=n", unified_defaults)

    def test_nimble_does_not_log_every_streaming_notification(self):
        unified_defaults = (ROOT / "sdkconfig.unified.defaults").read_text()

        self.assertIn("CONFIG_BT_NIMBLE_LOG_LEVEL_WARNING=y", unified_defaults)
        self.assertNotIn("CONFIG_BT_NIMBLE_LOG_LEVEL_INFO=y", unified_defaults)

    def test_transport_uses_persisted_random_identity_to_avoid_stale_pairing_cache(self):
        source = (ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp").read_text()

        self.assertIn('#include <hal/utils/settings/settings.h>', source)
        self.assertIn('Settings settings("codex_ble", true)', source)
        self.assertIn('settings.GetBlob("identity"', source)
        self.assertIn("ble_hs_id_gen_rnd(0, &generated)", source)
        self.assertIn('settings.SetBlob("identity"', source)
        self.assertIn("ble_hs_id_set_rnd(address.data())", source)
        self.assertIn("ble_hs_util_ensure_addr(1)", source)

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
        pipeline_header = (
            ROOT / "main/apps/app_codex_status/voice_capture_pipeline.h"
        ).read_text()
        shutdown = controller.split("void BleVoiceProbeController::shutdown()", 1)[1].split(
            "void BleVoiceProbeController::producer_entry", 1
        )[0]
        self.assertIn("kProducerStopTimeout", shutdown)
        self.assertIn("kCaptureReadTimeoutMs = 100", pipeline_header)
        self.assertIn("_capture_pipeline.requestStop()", shutdown)
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

    def test_audio_backend_starts_shared_tx_clock_before_rx_capture(self):
        backend = (ROOT / "main/hal/audio/stopwatch_audio_backend.cpp").read_text()
        open_recorder = backend.split(
            "bool StopWatchAudioBackend::openRecorder", 1
        )[1].split("void StopWatchAudioBackend::closeRecorder", 1)[0]
        self.assertIn("enableTxLocked()", open_recorder)
        self.assertIn("enableRxLocked()", open_recorder)
        self.assertLess(
            open_recorder.index("enableTxLocked()"),
            open_recorder.index("enableRxLocked()"),
        )

    def test_microphone_capture_buffer_does_not_consume_worker_stack(self):
        header = (
            ROOT / "main/apps/app_codex_status/voice_capture_pipeline.h"
        ).read_text()
        pipeline = (
            ROOT / "main/apps/app_codex_status/voice_capture_pipeline.cpp"
        ).read_text()
        capture = pipeline.split(
            "VoiceCapturePipeline::captureBlock", 1
        )[1].split("void VoiceCapturePipeline::requestStop", 1)[0]
        self.assertIn("_capture_input", header)
        self.assertNotIn("std::array<int16_t, kCaptureInputSamples> captured", capture)

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
            "void BleVoiceProbeController::transition_microphone_failure", 1
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

    def test_crypto_failure_replaces_failed_data_with_counter_summary(self):
        header = (
            ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.h"
        ).read_text()
        controller = (
            ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.cpp"
        ).read_text()
        policy = (
            ROOT
            / "main/apps/app_codex_status/ble_voice_probe_failure_transition.cpp"
        ).read_text()
        self.assertIn("prepare_crypto_failure_summary_locked", header)

        summary_failure = controller.split(
            "bool BleVoiceProbeController::prepare_crypto_failure_summary_locked", 1
        )[1].split(
            "void BleVoiceProbeController::prepare_terminal_error_locked", 1
        )[0]
        self.assertIn("prepare_crypto_failure_transition(", summary_failure)
        self.assertIn("_pending_summary = transition.summary", summary_failure)
        self.assertIn("_pending_data = {};", summary_failure)
        self.assertIn("_pending_data_valid = false;", summary_failure)
        self.assertIn("_stop_requested = transition.stop_requested", summary_failure)

        self.assertIn("candidate.summary.kind = Kind::StopSummary", policy)
        self.assertIn(
            "candidate.summary.sequence = failed_data.sequence",
            policy,
        )
        self.assertIn(
            "candidate.summary.payload_length = encode_stop_summary(",
            policy,
        )

        synthetic = controller.split(
            "bool BleVoiceProbeController::prepare_synthetic_packet_locked", 1
        )[1].split(
            "bool BleVoiceProbeController::queue_start_ack_locked", 1
        )[0]
        self.assertIn("prepare_crypto_failure_summary_locked(now_ms)", synthetic)

        microphone_packet = controller.split(
            "bool BleVoiceProbeController::prepare_microphone_packet", 1
        )[1].split(
            "void BleVoiceProbeController::transition_microphone_failure", 1
        )[0]
        self.assertIn("_pending_data_valid = true;", microphone_packet)
        microphone_failure = controller.split(
            "void BleVoiceProbeController::transition_microphone_failure", 1
        )[1].split(
            "void BleVoiceProbeController::release_audio_resources", 1
        )[0]
        self.assertIn(
            "error == ble_voice_probe::ErrorCode::CryptoFailed",
            microphone_failure,
        )
        self.assertIn(
            "prepare_crypto_failure_summary_locked(now_ms)",
            microphone_failure,
        )

    def test_microphone_failure_transition_has_single_lock_owner(self):
        controller = (
            ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.cpp"
        ).read_text()
        self.assertNotIn("xSemaphoreCreateRecursiveMutex", controller)
        self.assertNotIn("xSemaphoreTakeRecursive", controller)
        producer = controller.split(
            "void BleVoiceProbeController::producer_loop", 1
        )[1].split(
            "bool BleVoiceProbeController::prepare_synthetic_packet_locked", 1
        )[0]
        packet_failure = producer.split(
            "if (!prepare_microphone_packet(", 1
        )[1].split("} else if (capture_result", 1)[0]
        self.assertNotIn("StateLock", packet_failure)
        self.assertEqual(
            packet_failure.count("transition_microphone_failure(error)"), 1
        )

        transition = controller.split(
            "void BleVoiceProbeController::transition_microphone_failure", 1
        )[1].split(
            "void BleVoiceProbeController::release_audio_resources", 1
        )[0]
        self.assertEqual(transition.count("StateLock lock(_state_mutex)"), 1)
        self.assertIn("prepare_crypto_failure_summary_locked(now_ms)", transition)

        synthetic = controller.split(
            "bool BleVoiceProbeController::prepare_synthetic_packet_locked", 1
        )[1].split(
            "bool BleVoiceProbeController::queue_start_ack_locked", 1
        )[0]
        self.assertNotIn("StateLock", synthetic)
        self.assertIn("prepare_crypto_failure_summary_locked(now_ms)", synthetic)

    def test_microphone_producer_drains_complete_pcm_before_more_reads(self):
        pipeline = (
            ROOT / "main/apps/app_codex_status/voice_capture_pipeline.cpp"
        ).read_text()
        capture = pipeline.split("VoiceCapturePipeline::captureBlock", 1)[1].split(
            "void VoiceCapturePipeline::requestStop", 1
        )[0]
        helper = pipeline.split("VoiceCapturePipeline::encodePendingBlock", 1)[1]
        self.assertIn("encodePendingBlock", capture)
        self.assertIn("_microphone_pcm.take_complete_block", helper)
        self.assertIn("_backend->readRecorder", capture)
        self.assertLess(
            capture.index("encodePendingBlock"),
            capture.index("_backend->readRecorder"),
        )

    def test_microphone_resources_are_acquired_and_released(self):
        pipeline = (
            ROOT / "main/apps/app_codex_status/voice_capture_pipeline.cpp"
        ).read_text()
        controller = (
            ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.cpp"
        ).read_text()
        self.assertIn("acquireCapture(RuntimeOwner::Codex)", pipeline)
        self.assertIn("releaseCapture(RuntimeOwner::Codex)", pipeline)
        self.assertIn("openRecorder(kCaptureRate, 16, 1)", pipeline)
        self.assertIn("closeRecorder()", pipeline)
        self.assertIn("esp_ae_rate_cvt_open", pipeline)
        self.assertIn("esp_ae_rate_cvt_close", pipeline)
        self.assertIn("encode_ima_adpcm_block", pipeline)
        self.assertIn("record_captured_samples", controller)

    def test_offline_validation_and_probe_docs_cover_pcm_and_checkout_evidence(self):
        runner = (ROOT / "scripts/run_unified_offline_validation.sh").read_text()
        docs = (ROOT / "docs/ble-voice-link-probe.md").read_text()
        self.assertIn('stage "BLE voice probe crypto host test"', runner)
        self.assertIn("tests/voice_probe_crypto_test.cpp", runner)
        self.assertIn('stage "Production voice crypto host test"', runner)
        self.assertIn("tests/voice_input_crypto_test.cpp", runner)
        self.assertIn('stage "Production voice protocol host test"', runner)
        self.assertIn("tests/voice_input_protocol_test.cpp", runner)
        self.assertIn('stage "Production voice session host test"', runner)
        self.assertIn("tests/voice_input_session_test.cpp", runner)
        self.assertIn("tests/voice_input_controller_test.cpp", runner)
        self.assertIn("main/apps/app_codex_status/voice_input_controller.cpp", runner)
        self.assertIn('stage "BLE voice probe PCM buffer host test"', runner)
        self.assertIn('stage "Production voice capture pipeline host test"', runner)
        self.assertIn("tests/ble_voice_probe_pcm_buffer_test.cpp", runner)
        self.assertIn("tests/voice_capture_pipeline_test.cpp", runner)
        self.assertIn(
            'stage "BLE voice probe failure transition host test"', runner
        )
        self.assertIn('stage "Whitespace error check"', runner)
        self.assertLess(
            runner.index("tests/voice_probe_crypto_test.cpp"),
            runner.index("./scripts/build_unified_candidate.sh --clean"),
        )
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
