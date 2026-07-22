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

    def test_controller_shutdown_is_bounded_and_owns_worker_deletion(self):
        controller = (
            ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.cpp"
        ).read_text()
        shutdown = controller.split("void BleVoiceProbeController::shutdown()", 1)[1].split(
            "void BleVoiceProbeController::producer_entry", 1
        )[0]
        self.assertIn("kProducerStopTimeout", shutdown)
        self.assertIn(
            "vTaskDelete(static_cast<TaskHandle_t>(_producer_task))", shutdown
        )
        self.assertNotIn("portMAX_DELAY", shutdown)
        self.assertIn("vTaskSuspend(nullptr)", controller)


if __name__ == "__main__":
    unittest.main()
