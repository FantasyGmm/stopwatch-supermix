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


if __name__ == "__main__":
    unittest.main()
