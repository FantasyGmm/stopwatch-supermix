import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class LowBatteryAlertFirmwareContractTests(unittest.TestCase):
    def test_hal_exposes_low_battery_observations(self):
        header = (ROOT / "main" / "hal" / "hal.h").read_text(encoding="utf-8")
        pmic = (ROOT / "main" / "hal" / "hal_pmic.cpp").read_text(encoding="utf-8")
        display = (ROOT / "main" / "hal" / "hal_display.cpp").read_text(encoding="utf-8")
        audio = (ROOT / "main" / "hal" / "hal_audio.cpp").read_text(encoding="utf-8")

        for token in ("batterySampleRevision()", "isDisplayVisible()", "isAudioPlaying()"):
            self.assertIn(token, header)
        self.assertIn("_bat_sample_revision++", pmic)
        self.assertIn("uint32_t Hal::batterySampleRevision()", pmic)
        self.assertIn("bool Hal::isDisplayVisible()", display)
        self.assertIn("return _bl_brightness > 0", display)
        self.assertIn("bool isPlaying()", audio)
        self.assertIn("bool Hal::isAudioPlaying()", audio)


if __name__ == "__main__":
    unittest.main()
