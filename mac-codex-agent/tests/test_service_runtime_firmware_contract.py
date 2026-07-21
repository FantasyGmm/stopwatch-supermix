import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ServiceRuntimeFirmwareContractTests(unittest.TestCase):
    def test_brookesia_bootstrap_dependencies_are_zero_seven_only(self):
        manifest = (ROOT / "main" / "idf_component.yml").read_text(
            encoding="utf-8"
        )
        for component in (
            "espressif/brookesia_service_manager",
            "espressif/brookesia_service_helper",
            "espressif/brookesia_hal_interface",
        ):
            self.assertRegex(
                manifest,
                re.compile(
                    rf"(?m)^\s{{2}}{re.escape(component)}:\s*\n"
                    rf"\s+version:\s*['\"]?>=0\.7\.0,<0\.8\.0['\"]?\s*$"
                ),
                component,
            )
        self.assertNotIn("brookesia_hal_adaptor", manifest)
        self.assertNotIn("brookesia_hal_boards", manifest)

    def test_service_runtime_starts_after_hal_and_before_apps(self):
        main_cpp = (ROOT / "main" / "main.cpp").read_text(encoding="utf-8")

        hal_index = main_cpp.index("GetHAL().init();")
        runtime_index = main_cpp.index("GetServiceRuntime().start()")
        app_index = main_cpp.index("GetMooncake().installApp")

        self.assertLess(hal_index, runtime_index)
        self.assertLess(runtime_index, app_index)
        self.assertIn("if (!GetServiceRuntime().start())", main_cpp)

    def test_service_runtime_uses_one_shot_manager_start(self):
        source_path = ROOT / "main" / "runtime" / "service_runtime.cpp"
        self.assertTrue(source_path.is_file())
        source = source_path.read_text(encoding="utf-8")

        self.assertIn("if (_started)", source)
        self.assertEqual(source.count("ServiceManager::get_instance()"), 1)
        self.assertEqual(source.count("manager.start()"), 1)

    def test_runtime_sources_are_part_of_the_main_component(self):
        cmake = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('"runtime/*.cpp"', cmake)

    def test_wifi_driver_keeps_station_and_config_ap_startup_distinct(self):
        source = (
            ROOT / "main" / "runtime" / "services" / "stopwatch_wifi_driver.cpp"
        ).read_text(encoding="utf-8")
        start_sta = source.split("bool startSta() override", 1)[1].split(
            "void stopSta() override", 1
        )[0]
        start_config_ap = source.split("bool startConfigAp() override", 1)[1].split(
            "void stopConfigAp() override", 1
        )[0]

        self.assertIn("wifi.StartStation();", start_sta)
        self.assertEqual(start_sta.count("wifi.StartStation();"), 1)
        self.assertRegex(
            start_sta,
            r"return false;\s*}\s*wifi\.StartStation\(\);",
        )
        self.assertNotIn("SsidManager", start_sta)
        self.assertNotIn("wifi.StartConfigAp();", start_sta)
        self.assertRegex(
            start_config_ap,
            r"wifi\.StartConfigAp\(\);\s*return\s+wifi\.IsConfigMode\(\);",
        )


if __name__ == "__main__":
    unittest.main()
