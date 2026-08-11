import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class MultiAgentBleFirmwareContractTests(unittest.TestCase):
    def test_transport_routes_active_and_pending_handles(self):
        header = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.h"
        ).read_text()
        source = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp"
        ).read_text()

        self.assertIn("AgentConnectionRouter", header)
        self.assertIn("promote_pending", header)
        self.assertIn("reject_pending", header)
        self.assertIn("take_pairing_frame", header)
        self.assertIn("notify_pairing", header)
        self.assertIn("authorize_gatt", header)
        self.assertIn("AgentGattSurface::Production", source)
        self.assertIn("AgentGattSurface::Pairing", source)
        self.assertIn("uint16_t connection_handle", source)
        self.assertIn("event->disconnect.conn.conn_handle", source)
        self.assertIn("event->subscribe.conn_handle", source)
        self.assertIn("_connection_router.active_handle()", source)
        self.assertNotIn("std::atomic<uint16_t> _connection_handle", header)
        self.assertNotIn("std::atomic<bool> _connected", header)

    def test_pairing_and_production_gatt_surfaces_are_separate(self):
        source = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp"
        ).read_text()
        self.assertIn("kPairingServiceUuid", source)
        self.assertIn("kPairingControlUuid", source)
        self.assertIn("kPairingTxUuid", source)
        self.assertIn("pairing_control_access", source)
        self.assertIn("pairing_tx_access", source)
        self.assertIn("kPairingCharacteristics", source)
        self.assertIn("BLE_ATT_ERR_INSUFFICIENT_AUTHOR", source)

        status_access = source.split("int gatt_access", 1)[1].split(
            "int voice_control_access", 1
        )[0]
        voice_access = source.split("int voice_control_access", 1)[1].split(
            "int voice_tx_access", 1
        )[0]
        pairing_access = source.split("int pairing_control_access", 1)[1].split(
            "int pairing_tx_access", 1
        )[0]
        self.assertIn("AgentGattSurface::Production", status_access)
        self.assertIn("AgentGattSurface::Production", voice_access)
        self.assertIn("AgentGattSurface::Pairing", pairing_access)

    def test_nimble_is_bounded_to_exactly_two_connections(self):
        defaults = (ROOT / "sdkconfig.defaults").read_text()
        generated = (ROOT / "sdkconfig").read_text()
        self.assertIn("CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2", defaults)
        self.assertIn("CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2", generated)
        self.assertNotIn("CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1", defaults)
        self.assertIn("CONFIG_BT_NIMBLE_MAX_CCCDS=2", defaults)


if __name__ == "__main__":
    unittest.main()
