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


if __name__ == "__main__":
    unittest.main()
