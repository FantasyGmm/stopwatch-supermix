import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class AudioServiceFirmwareContractTests(unittest.TestCase):
    def test_audio_service_dependencies_are_zero_seven_only(self):
        manifest = (ROOT / "main" / "idf_component.yml").read_text(
            encoding="utf-8"
        )
        for component in (
            "espressif/brookesia_service_device",
            "espressif/brookesia_service_audio",
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

    def test_stopwatch_audio_device_adapts_both_interfaces(self):
        header = (
            ROOT
            / "main"
            / "runtime"
            / "services"
            / "stopwatch_audio_device.h"
        ).read_text(encoding="utf-8")
        self.assertIn("AudioCodecPlayerIface", header)
        self.assertIn("AudioCodecRecorderIface", header)
        self.assertIn("class StopWatchAudioDevice", header)

    def test_audio_device_initializes_before_service_manager(self):
        source = (ROOT / "main" / "runtime" / "service_runtime.cpp").read_text(
            encoding="utf-8"
        )
        device_index = source.index("InitializeStopWatchAudioDevice()")
        manager_index = source.index("ServiceManager::get_instance()")
        self.assertLess(device_index, manager_index)
        self.assertIn("StopWatchAudioInterfacesReady()", source)


if __name__ == "__main__":
    unittest.main()
