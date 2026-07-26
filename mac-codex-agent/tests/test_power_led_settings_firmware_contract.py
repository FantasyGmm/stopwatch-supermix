import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class PowerLedSettingsFirmwareContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.hal_header = (ROOT / "main" / "hal" / "hal.h").read_text(
            encoding="utf-8"
        )
        cls.hal_pmic = (ROOT / "main" / "hal" / "hal_pmic.cpp").read_text(
            encoding="utf-8"
        )
        cls.main_loop = (ROOT / "main" / "main.cpp").read_text(encoding="utf-8")
        cls.setup_app = (
            ROOT / "main" / "apps" / "app_setup" / "app_setup.cpp"
        ).read_text(encoding="utf-8")
        cls.workers_header = (
            ROOT / "main" / "apps" / "app_setup" / "workers" / "workers.h"
        ).read_text(encoding="utf-8")
        cls.device_worker = (
            ROOT / "main" / "apps" / "app_setup" / "workers" / "device.cpp"
        ).read_text(encoding="utf-8")

    def test_hal_exposes_three_persisted_modes(self):
        self.assertIn("enum class PowerLedMode", self.hal_header)
        self.assertIn("Off = 0", self.hal_header)
        self.assertIn("On = 1", self.hal_header)
        self.assertIn("Blink = 2", self.hal_header)
        self.assertIn("void setPowerLedMode(PowerLedMode mode", self.hal_header)
        self.assertIn("PowerLedMode getPowerLedMode", self.hal_header)
        self.assertIn("void updatePowerLed()", self.hal_header)

        self.assertIn('GetInt("pwr_led_mode", 0)', self.hal_pmic)
        self.assertIn('SetInt("pwr_led_mode"', self.hal_pmic)
        self.assertIn("setLedEnLevel", self.hal_pmic)

    def test_blink_mode_uses_short_pulse_on_two_second_period(self):
        self.assertIn("constexpr uint32_t _power_led_blink_period_ms = 2000", self.hal_pmic)
        self.assertIn("constexpr uint32_t _power_led_pulse_ms = 160", self.hal_pmic)
        self.assertIn("void Hal::updatePowerLed()", self.hal_pmic)
        self.assertIn("GetHAL().updatePowerLed();", self.main_loop)

    def test_settings_registers_power_led_worker(self):
        self.assertIn('"Power LED"', self.setup_app)
        self.assertIn("std::make_unique<PowerLedWorker>()", self.setup_app)
        self.assertIn("class PowerLedWorker : public WorkerBase", self.workers_header)

    def test_power_led_page_previews_commits_and_restores_mode(self):
        self.assertIn("PowerLedWorker::PowerLedWorker()", self.device_worker)
        self.assertIn("PowerLedWorker::~PowerLedWorker()", self.device_worker)
        self.assertIn("GetHAL().setPowerLedMode", self.device_worker)
        self.assertIn("_committed", self.device_worker)
        self.assertIn('"Off"', self.device_worker)
        self.assertIn('"On"', self.device_worker)
        self.assertIn('"Blink 2s"', self.device_worker)
        self.assertIn('label().setText("OK")', self.device_worker)


if __name__ == "__main__":
    unittest.main()
