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


if __name__ == "__main__":
    unittest.main()
