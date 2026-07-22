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

    def test_player_queues_pcm_before_blocking_i2s_output(self):
        source = (
            ROOT
            / "main"
            / "runtime"
            / "services"
            / "stopwatch_audio_device.cpp"
        ).read_text(encoding="utf-8")
        write_body = source.split("bool StopWatchPlayer::write_data", 1)[1].split(
            "bool StopWatchPlayer::is_pa_on_off_supported", 1
        )[0]
        self.assertIn("_impl->enqueue", write_body)
        self.assertNotIn("_backend.write", write_body)
        self.assertIn("xRingbufferSend", source)
        self.assertIn("xRingbufferCreateWithCaps", source)
        self.assertIn("MALLOC_CAP_SPIRAM", source)
        self.assertIn("backend.write", source)

    def test_backend_allows_full_duplex_i2s_io(self):
        source = (
            ROOT / "main" / "hal" / "audio" / "stopwatch_audio_backend.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("recorder_io_mutex", source)
        self.assertIn("player_io_mutex", source)
        self.assertNotIn("mutable std::mutex mutex;", source)

    def test_assistant_maps_zero_volume_to_brookesia_mute(self):
        source = (
            ROOT / "main" / "runtime" / "assistant" / "assistant_runtime.cpp"
        ).read_text(encoding="utf-8")
        configure_body = source.split(
            "bool AssistantRuntime::Impl::configureAudioAndAgent()", 1
        )[1].split("bool AssistantRuntime::Impl::", 1)[0]

        self.assertIn("AudioHelper::FunctionId::SetVolume", configure_body)
        self.assertIn("AudioHelper::FunctionId::SetMute", configure_body)
        self.assertIn("volume <= 0", configure_body)

if __name__ == "__main__":
    unittest.main()
