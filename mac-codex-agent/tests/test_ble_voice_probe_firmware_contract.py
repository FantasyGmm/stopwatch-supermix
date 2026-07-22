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


if __name__ == "__main__":
    unittest.main()
