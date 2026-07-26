import re
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
        self.assertIn(
            "void setPowerLedBlinkIntervalSeconds(int seconds", self.hal_header
        )
        self.assertIn("int getPowerLedBlinkIntervalSeconds", self.hal_header)
        self.assertIn("void updatePowerLed()", self.hal_header)

        self.assertIn('GetInt("pwr_led_mode", 0)', self.hal_pmic)
        self.assertIn('SetInt("pwr_led_mode"', self.hal_pmic)
        self.assertIn(
            'GetInt("pwr_led_int_s", PowerLedBlinkIntervalDefaultSeconds)',
            self.hal_pmic,
        )
        self.assertIn('SetInt("pwr_led_int_s"', self.hal_pmic)
        self.assertIn("setLedEnLevel", self.hal_pmic)

    def test_power_led_nvs_keys_fit_esp_idf_limit(self):
        keys = re.findall(r'(?:GetInt|SetInt)\("([^"]+)"', self.hal_pmic)
        self.assertGreater(len(keys), 0)
        self.assertEqual([], [key for key in keys if len(key) > 15])

    def test_blink_mode_uses_short_pulse_on_configurable_period(self):
        self.assertIn("constexpr uint32_t _power_led_pulse_ms = 160", self.hal_pmic)
        self.assertIn(
            "power_led_blink_interval_ms(_power_led_blink_interval_seconds)",
            self.hal_pmic,
        )
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
        self.assertIn('"Blink"', self.device_worker)
        self.assertIn('label().setText("OK")', self.device_worker)

    def test_power_led_page_shows_interval_slider_only_for_blink(self):
        self.assertIn(
            'createModeRow(160, "Blink", Hal::PowerLedMode::Blink)',
            self.device_worker,
        )
        self.assertIn("_interval_slider->setRange(1, 10, false);", self.device_worker)
        self.assertIn(
            'std::snprintf(buffer, sizeof(buffer), "Interval: %ds",',
            self.device_worker,
        )
        self.assertIn(
            "setHidden(_current_mode != Hal::PowerLedMode::Blink)",
            self.device_worker,
        )

    def test_power_led_worker_previews_commits_and_restores_interval(self):
        self.assertIn("GetHAL().setPowerLedBlinkIntervalSeconds", self.device_worker)
        self.assertIn("_original_interval_seconds", self.workers_header)
        self.assertIn("_applied_interval_seconds", self.workers_header)


if __name__ == "__main__":
    unittest.main()
