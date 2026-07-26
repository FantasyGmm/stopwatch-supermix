import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class DebugCommandFirmwareContractTests(unittest.TestCase):
    def test_usb_debug_command_is_gated_and_task_free(self):
        kconfig = (ROOT / "main" / "Kconfig.projbuild").read_text(encoding="utf-8")
        defaults = (ROOT / "sdkconfig.unified.defaults").read_text(encoding="utf-8")
        cmake = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
        main = (ROOT / "main" / "main.cpp").read_text(encoding="utf-8")
        service = (
            ROOT / "main" / "debug" / "debug_command_service.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("config STOPWATCH_DEBUG_COMMANDS", kconfig)
        self.assertIn("default n", kconfig)
        self.assertIn("CONFIG_STOPWATCH_DEBUG_COMMANDS=y", defaults)
        self.assertIn("CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y", defaults)
        self.assertIn('"debug/*.cpp"', cmake)
        self.assertIn("GetDebugCommandService().update();", main)
        self.assertIn('#include <hal/usb_serial_jtag_ll.h>', service)
        self.assertIn("usb_serial_jtag_ll_read_rxfifo", service)
        self.assertNotIn("/dev/secondary", service)
        for forbidden in (
            "usb_serial_jtag_driver_install",
            "read(STDIN_FILENO",
            "xTaskCreate",
            "std::thread",
            "while (1)",
        ):
            self.assertNotIn(forbidden, service)

    def test_launcher_owns_debug_open_lifecycle(self):
        launcher = (
            ROOT / "main" / "apps" / "app_launcher" / "app_launcher.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("RegisterDebugAppTarget(this)", launcher)
        self.assertIn("UnregisterDebugAppTarget(this)", launcher)
        self.assertIn("GetMooncake().closeApp(getRunningAppId())", launcher)
        self.assertIn("DebugAppNameEquals(props.info.name, name)", launcher)
        self.assertIn("openApp(props.appID)", launcher)
        self.assertNotIn("GetMooncake().openApp(props.appID)", launcher)

    def test_display_protection_status_is_read_only_metadata(self):
        parser_header = (
            ROOT / "main" / "debug" / "debug_command_parser.h"
        ).read_text(encoding="utf-8")
        service = (
            ROOT / "main" / "debug" / "debug_command_service.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("DisplayProtectionStatus", parser_header)
        self.assertIn(
            "GetGlobalDisplayProtectionService().snapshot()", service
        )
        required_fields = (
            "state=%s",
            "effective=%d",
            "user=%d",
            "automatic=%u",
            "power=%s",
            "power_valid=%u",
            "dim_choice=%u",
            "off_choice=%u",
            'dim_ms=%" PRIu32',
            'off_ms=%" PRIu32',
            'idle_ms=%" PRIu32',
            'dim_remaining_ms=%" PRIu32',
            'off_remaining_ms=%" PRIu32',
            "charging_guard=%u",
            "reasons=0x%02x",
            "last_wake=%s",
        )
        self.assertIn('"DISPLAY PROTECTION state=%s ', service)
        for field in required_fields:
            self.assertIn(field, service)
        body_start = service.index("void log_display_protection_status()")
        body_end = service.index("\n}", body_start)
        display_status_body = service[body_start:body_end].lower()
        for forbidden in ("transcript", "payload", "audio data", "conversation"):
            self.assertNotIn(forbidden, display_status_body)

    def test_display_protection_status_has_stable_complete_enum_names(self):
        service = (
            ROOT / "main" / "debug" / "debug_command_service.cpp"
        ).read_text(encoding="utf-8")

        expected_mappings = {
            "DisplayProtectionState::Active": '"active"',
            "DisplayProtectionState::Dimmed": '"dimmed"',
            "DisplayProtectionState::ScreenOff": '"off"',
            "ExternalPowerState::Unknown": '"unknown"',
            "ExternalPowerState::Battery": '"battery"',
            "ExternalPowerState::ExternalPower": '"external"',
            "DisplayWakeReason::Initialization": '"initialization"',
            "DisplayWakeReason::Button": '"button"',
            "DisplayWakeReason::Touch": '"touch"',
            "DisplayWakeReason::PowerInserted": '"power_inserted"',
            "DisplayWakeReason::PowerRemoved": '"power_removed"',
            "DisplayWakeReason::ConfigurationChanged": '"configuration_changed"',
            "DisplayWakeReason::KeepAwakeActivated": '"keep_awake_activated"',
        }
        for enum_value, ascii_name in expected_mappings.items():
            self.assertRegex(
                service,
                rf"case {enum_value}:\s+return {ascii_name};",
            )

        helper_starts = [
            service.index("const char* display_protection_state_name("),
            service.index("const char* external_power_state_name("),
            service.index("const char* display_wake_reason_name("),
        ]
        for helper_start in helper_starts:
            helper_end = service.index("\n}", helper_start)
            self.assertIn('return "unknown";', service[helper_start:helper_end])

    def test_display_protection_logs_transitions_without_sample_spam(self):
        service = (
            ROOT
            / "main"
            / "runtime"
            / "display"
            / "global_display_protection_service.cpp"
        ).read_text(encoding="utf-8")
        pmic = (ROOT / "main" / "hal" / "hal_pmic.cpp").read_text(
            encoding="utf-8"
        )

        for token in (
            "power transition",
            "configuration committed",
            "display action",
            "power sample unhealthy",
            "power sample healthy",
            "PowerHealthRetryLogMs",
        ):
            self.assertIn(token, service)
        self.assertIn("latest_power.latest_sample_valid", service)
        self.assertIn("_last_power_health_log_ms", service)
        sampler_start = pmic.index("void bat_reading_task")
        sampler_end = pmic.index("\n}\n\n}", sampler_start)
        sampler_body = pmic[sampler_start:sampler_end]
        sampler_loop = sampler_body[sampler_body.index("while (true)") :]
        self.assertNotIn("mclog::", sampler_loop)


if __name__ == "__main__":
    unittest.main()
