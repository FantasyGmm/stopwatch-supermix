import pathlib
import re
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

    def test_custom_pet_service_has_exact_optional_authenticated_surface(self):
        header = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.h"
        ).read_text()
        source = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp"
        ).read_text()

        def uuid_for(symbol):
            match = re.search(
                rf"const ble_uuid128_t {symbol} = BLE_UUID128_INIT\((.*?)\n\);",
                source,
                re.DOTALL,
            )
            self.assertIsNotNone(match, symbol)
            octets = bytes(
                int(value, 16)
                for value in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1))
            )
            self.assertEqual(len(octets), 16, symbol)
            canonical = octets[::-1].hex()
            return "-".join(
                (
                    canonical[:8],
                    canonical[8:12],
                    canonical[12:16],
                    canonical[16:20],
                    canonical[20:],
                )
            )

        symbols = (
            "kServiceUuid",
            "kStatusRxUuid",
            "kVoiceControlUuid",
            "kVoiceTxUuid",
            "kVoiceServiceUuid",
            "kPairingServiceUuid",
            "kPairingControlUuid",
            "kPairingTxUuid",
            "kInputControlUuid",
            "kInputTxUuid",
            "kPetServiceUuid",
            "kPetControlUuid",
            "kPetStatusUuid",
        )
        self.assertEqual(
            [uuid_for(symbol) for symbol in symbols],
            [
                f"7a2300{suffix:02x}-5474-4f50-5741-544348434f44"
                for suffix in range(1, 14)
            ],
        )

        for symbol in (
            "CodexPetControlFrame",
            "take_pet_control",
            "pet_subscribed",
            "notify_pet_status",
            "set_pet_credential",
            "clear_pet_credential",
        ):
            self.assertIn(symbol, header)
        self.assertIn("kPetControlQueueDepth = 8", source)
        self.assertIn("pet_control_access", source)
        self.assertIn("pet_status_access", source)
        pet_control_access = source.split("int pet_control_access", 1)[1].split(
            "int pet_status_access", 1
        )[0]
        pet_status_access = source.split("int pet_status_access", 1)[1].split(
            "const ble_gatt_chr_def kStatusCharacteristics", 1
        )[0]
        self.assertIn("AgentGattSurface::Production", pet_control_access)
        self.assertIn("AgentGattSurface::Production", pet_status_access)
        self.assertIn("kPetServices", source)
        enqueue = source.split(
            "bool CodexBleTransport::enqueue_pet_control_from_ble", 1
        )[1].split("bool CodexBleTransport::handle_gap_connected", 1)[0]
        self.assertIn("xQueueSend", enqueue)
        self.assertRegex(enqueue, r"&frame,\s*0\) == pdTRUE")
        pet_credentials = source.split(
            "bool CodexBleTransport::set_pet_credential", 1
        )[1].split("bool CodexBleTransport::authorize_gatt", 1)[0]
        self.assertNotIn("_production_voice_crypto", pet_credentials)
        self.assertNotIn("_input_control_guard", pet_credentials)
        pet_subscription = source.split(
            "void CodexBleTransport::handle_pet_subscription", 1
        )[1].split("void CodexBleTransport::handle_mtu_update", 1)[0]
        self.assertNotIn("_input_action_subscribed", pet_subscription)
        self.assertNotIn("update_voice_subscription", pet_subscription)

        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        pet_loop = app.split("void AppCodexStatus::process_pet_transfer", 1)[
            1
        ].split("void AppCodexStatus::onClose", 1)[0]
        self.assertIn("handled < codex_pet::kPetControlsPerLoop", pet_loop)
        self.assertIn("codex_pet::kPetLoopBudgetUs", pet_loop)

    def test_nimble_is_bounded_to_exactly_two_connections(self):
        defaults = (ROOT / "sdkconfig.defaults").read_text()
        generated = (ROOT / "sdkconfig").read_text()
        self.assertIn("CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2", defaults)
        self.assertIn("CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2", generated)
        self.assertNotIn("CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1", defaults)
        self.assertIn("CONFIG_BT_NIMBLE_MAX_CCCDS=2", defaults)


if __name__ == "__main__":
    unittest.main()
