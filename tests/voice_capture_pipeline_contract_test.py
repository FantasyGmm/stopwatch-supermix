import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
HOST_TEST = ROOT / "tests/voice_capture_pipeline_test.cpp"
PIPELINE_HEADER = (
    ROOT / "main/apps/app_codex_status/voice_capture_pipeline.h"
)
PIPELINE_SOURCE = (
    ROOT / "main/apps/app_codex_status/voice_capture_pipeline.cpp"
)
CONTROLLER_HEADER = (
    ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.h"
)
CONTROLLER_SOURCE = (
    ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.cpp"
)


class VoiceCapturePipelineContractTests(unittest.TestCase):
    def pipeline(self):
        self.assertTrue(PIPELINE_HEADER.exists(), "missing voice capture pipeline header")
        self.assertTrue(PIPELINE_SOURCE.exists(), "missing voice capture pipeline source")
        return PIPELINE_HEADER.read_text(), PIPELINE_SOURCE.read_text()

    def test_pipeline_exposes_bounded_capture_lifecycle(self):
        header, source = self.pipeline()
        self.assertIn("class VoiceCapturePipeline", header)
        self.assertIn("bool open(VoiceCaptureError& error)", header)
        self.assertIn("VoiceCaptureResult captureBlock(", header)
        self.assertIn("std::array<uint8_t, kAdpcmBytesPerBlock>& adpcm", header)
        self.assertIn("uint32_t& captured_samples", header)
        self.assertIn("void requestStop()", header)
        self.assertIn("void close()", header)
        self.assertIn("kCaptureReadTimeoutMs = 100", header)
        self.assertIn("_stop_requested", header)
        self.assertIn("std::atomic<bool>", header)
        self.assertIn("VoiceCapturePipeline::~VoiceCapturePipeline()", source)

    def test_real_host_lifecycle_stage_is_present(self):
        runner = (ROOT / "scripts/run_unified_offline_validation.sh").read_text()
        self.assertTrue(HOST_TEST.exists(), "missing executable pipeline host test")
        self.assertIn("tests/voice_capture_pipeline_test.cpp", runner)
        self.assertIn("run_host_test voice_capture_pipeline_test", runner)

    def test_pipeline_exclusively_owns_capture_resources_and_conversion(self):
        header, source = self.pipeline()
        self.assertIn(
            "GetResourceSupervisor().acquireCapture(RuntimeOwner::Codex)", source
        )
        self.assertIn(
            "GetStopWatchAudioBackend().openRecorder(kCaptureRate, 16, 1)", source
        )
        self.assertIn("GetStopWatchAudioBackend().closeRecorder()", source)
        self.assertIn(
            "GetResourceSupervisor().releaseCapture(RuntimeOwner::Codex)", source
        )
        self.assertIn("esp_ae_rate_cvt_open", source)
        self.assertIn("esp_ae_rate_cvt_process", source)
        self.assertIn("esp_ae_rate_cvt_close", source)
        self.assertIn("encode_ima_adpcm_block", source)
        self.assertIn("_capture_input", header)
        self.assertIn("_resampler_output", header)
        self.assertIn("_microphone_pcm", header)
        self.assertNotIn("std::array<int16_t, kCaptureInputSamples> captured", source)

    def test_pipeline_stays_transport_and_probe_policy_free(self):
        header, source = self.pipeline()
        pipeline = header + source
        for forbidden in (
            "notify_voice(",
            "ble_gatts_notify",
            "xQueue",
            "probe_duration",
            "fill_synthetic_payload",
            "StopSummary",
            "Notification",
        ):
            self.assertNotIn(forbidden, pipeline)

    def test_pipeline_drains_pcm_before_another_bounded_read(self):
        _, source = self.pipeline()
        capture = source.split("VoiceCapturePipeline::captureBlock", 1)[1].split(
            "void VoiceCapturePipeline::requestStop", 1
        )[0]
        helper = source.split("VoiceCapturePipeline::encodePendingBlock", 1)[1]
        self.assertIn("encodePendingBlock", capture)
        self.assertIn("_microphone_pcm.take_complete_block", helper)
        self.assertIn("_backend->readRecorder", capture)
        self.assertLess(
            capture.index("encodePendingBlock"),
            capture.index("_backend->readRecorder"),
        )
        self.assertIn("kCaptureReadTimeoutMs", capture)
        self.assertGreaterEqual(capture.count("_stop_requested.load()"), 2)

    def test_controller_only_delegates_capture_lifecycle(self):
        header = CONTROLLER_HEADER.read_text()
        source = CONTROLLER_SOURCE.read_text()
        self.assertIn('#include "voice_capture_pipeline.h"', header)
        self.assertIn("VoiceCapturePipeline _capture_pipeline", header)
        self.assertIn("_capture_pipeline.open", source)
        self.assertIn("_capture_pipeline.captureBlock", source)
        self.assertIn("_capture_pipeline.requestStop", source)
        self.assertIn("_capture_pipeline.close", source)
        for forbidden in (
            "openRecorder(",
            "closeRecorder(",
            "esp_ae_rate_cvt_",
            "encode_ima_adpcm_block",
            "_capture_input",
            "_resampler_output",
            "_microphone_pcm",
            "_capture_owned",
            "_recorder_open",
        ):
            self.assertNotIn(forbidden, header + source)


if __name__ == "__main__":
    unittest.main()
