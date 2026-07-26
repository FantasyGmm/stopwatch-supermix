import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class DisplayProtectionFirmwareContractTests(unittest.TestCase):
    def test_hal_separates_user_and_temporary_brightness(self):
        hal_header = (ROOT / "main" / "hal" / "hal.h").read_text(encoding="utf-8")
        display_source = (ROOT / "main" / "hal" / "hal_display.cpp").read_text(
            encoding="utf-8"
        )
        button_source = (ROOT / "main" / "hal" / "hal_button.cpp").read_text(
            encoding="utf-8"
        )

        for token in (
            "void setTemporaryBackLightBrightness(int brightness)",
            "int getUserBackLightBrightness() const",
            "uint32_t backLightBrightnessRevision() const",
            "ButtonPhysicalState readButtonPhysicalState() const",
            "void suppressButtonInputUntilRelease()",
        ):
            self.assertIn(token, hal_header)

        self.assertIn(
            "_button_input_gate.filter(readButtonPhysicalState())", button_source
        )
        temporary_start = display_source.index(
            "void Hal::setTemporaryBackLightBrightness"
        )
        temporary_body = display_source[
            temporary_start : display_source.index("\n}", temporary_start) + 2
        ]
        self.assertNotIn("Settings settings", temporary_body)

    def test_global_service_precedes_edge_controls_and_apps(self):
        main = (ROOT / "main" / "main.cpp").read_text(encoding="utf-8")
        service = (
            ROOT
            / "main"
            / "runtime"
            / "display"
            / "global_display_protection_service.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("GetGlobalDisplayProtectionService().init()", main)
        protection = main.index("GetGlobalDisplayProtectionService().update()")
        edge = main.index("GetGlobalEdgeControlService().update()")
        mooncake = main.index("GetMooncake().update()")
        self.assertLess(protection, edge)
        self.assertLess(edge, mooncake)
        self.assertIn(
            "GetGlobalDisplayProtectionService().applyPendingHardwareUpdate()", main
        )

        for token in (
            "lv_indev_wait_release(indev)",
            "lv_indev_reset(indev, nullptr)",
            "GetHAL().suppressButtonInputUntilRelease()",
            "GetHAL().setTemporaryBackLightBrightness",
        ):
            self.assertIn(token, service)
        self.assertNotIn("setBackLightBrightness", service)

    def test_codex_policy_tracks_external_power_and_preserves_manual_off(self):
        codex_source = (
            ROOT / "main" / "apps" / "app_codex_status" / "app_codex_status.cpp"
        ).read_text(encoding="utf-8")

        for token in (
            "kPowerPolicyCheckMs = 1000",
            "GetHAL().isBatteryCharging(false)",
            "setAutomaticProtection(!external_power)",
            "refresh_display_protection_policy(now, true)",
            "refresh_display_protection_policy(now, false)",
        ):
            self.assertIn(token, codex_source)

        close_body = codex_source[
            codex_source.index("void AppCodexStatus::onClose()") :
            codex_source.index("void AppCodexStatus::create_view()")
        ]
        manual_off_body = codex_source[
            codex_source.index("void AppCodexStatus::enter_screen_off()") :
            codex_source.index("void AppCodexStatus::wake_screen()")
        ]
        wake_body = codex_source[
            codex_source.index("void AppCodexStatus::wake_screen()") :
            codex_source.index("void AppCodexStatus::set_percent")
        ]
        self.assertIn("setAutomaticProtection(true)", close_body)
        self.assertIn("setAutomaticProtection(false, false)", manual_off_body)
        self.assertIn("setTemporaryBackLightBrightness(0)", manual_off_body)
        self.assertIn("setTemporaryBackLightBrightness", wake_body)


if __name__ == "__main__":
    unittest.main()
