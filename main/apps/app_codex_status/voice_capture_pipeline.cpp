#include "voice_capture_pipeline.h"

#include "ima_adpcm_codec.h"

#include <hal/audio/stopwatch_audio_backend.h>
#include <esp_ae_rate_cvt.h>
#include <freertos/FreeRTOS.h>
#include <runtime/resource_supervisor.h>

namespace {

constexpr uint32_t kCaptureRate = 44100;
constexpr uint32_t kOutputRate = 16000;
constexpr uint32_t kMaxResamplerOutputSamples = 512;

}  // namespace

VoiceCapturePipeline::~VoiceCapturePipeline()
{
    close();
}

bool VoiceCapturePipeline::open(VoiceCaptureError& error)
{
    if (_open) {
        error = VoiceCaptureError::None;
        return true;
    }

    close();
    _stop_requested = false;
    error = VoiceCaptureError::None;

    if (GetResourceSupervisor().acquireCapture(RuntimeOwner::Codex) !=
        AcquireResult::Granted) {
        error = VoiceCaptureError::CaptureBusy;
        _stop_requested = true;
        return false;
    }
    _capture_owned = true;

    if (!GetStopWatchAudioBackend().openRecorder(44100, 16, 1)) {
        error = VoiceCaptureError::RecorderOpenFailed;
        close();
        return false;
    }
    _recorder_open = true;

    esp_ae_rate_cvt_cfg_t config = {};
    config.src_rate = kCaptureRate;
    config.dest_rate = kOutputRate;
    config.channel = 1;
    config.bits_per_sample = 16;
    config.complexity = 3;
    config.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY;
    if (esp_ae_rate_cvt_open(&config, &_resampler) != ESP_AE_ERR_OK ||
        _resampler == nullptr) {
        error = VoiceCaptureError::ResamplerFailed;
        close();
        return false;
    }

    uint32_t output_capacity = 0;
    if (esp_ae_rate_cvt_get_max_out_sample_num(
            _resampler,
            kCaptureInputSamples,
            &output_capacity) != ESP_AE_ERR_OK ||
        output_capacity == 0 || output_capacity > kMaxResamplerOutputSamples) {
        error = VoiceCaptureError::ResamplerFailed;
        close();
        return false;
    }

    _resampler_output_capacity = output_capacity;
    _resampler_output.assign(_resampler_output_capacity, 0);
    _microphone_pcm.clear();
    _open = true;
    return true;
}

VoiceCaptureResult VoiceCapturePipeline::captureBlock(
    std::array<uint8_t, kAdpcmBytesPerBlock>& adpcm,
    uint32_t& captured_samples,
    VoiceCaptureError& error
)
{
    captured_samples = 0;
    error = VoiceCaptureError::None;

    VoiceCaptureResult result = encodePendingBlock(adpcm, captured_samples, error);
    if (result != VoiceCaptureResult::Continue) {
        return result;
    }
    if (_stop_requested.load()) {
        return VoiceCaptureResult::Stopped;
    }
    if (!_open || _resampler == nullptr || !_recorder_open ||
        _resampler_output_capacity == 0 ||
        _resampler_output.size() != _resampler_output_capacity) {
        error = VoiceCaptureError::ResamplerFailed;
        return VoiceCaptureResult::Failed;
    }

    const size_t expected_bytes =
        _capture_input.size() * sizeof(_capture_input.front());
    if (GetStopWatchAudioBackend().read(
            _capture_input.data(),
            expected_bytes,
            pdMS_TO_TICKS(kCaptureReadTimeoutMs)) != expected_bytes) {
        if (_stop_requested.load()) {
            return VoiceCaptureResult::Stopped;
        }
        error = VoiceCaptureError::RecorderOpenFailed;
        return VoiceCaptureResult::Failed;
    }
    if (_stop_requested.load()) {
        return VoiceCaptureResult::Stopped;
    }

    uint32_t output_samples = _resampler_output_capacity;
    if (esp_ae_rate_cvt_process(
            _resampler,
            static_cast<esp_ae_sample_t>(_capture_input.data()),
            _capture_input.size(),
            static_cast<esp_ae_sample_t>(_resampler_output.data()),
            &output_samples) != ESP_AE_ERR_OK ||
        output_samples > _resampler_output_capacity) {
        error = VoiceCaptureError::ResamplerFailed;
        return VoiceCaptureResult::Failed;
    }

    _microphone_pcm.append(_resampler_output.data(), output_samples);
    return encodePendingBlock(adpcm, captured_samples, error);
}

void VoiceCapturePipeline::requestStop()
{
    _stop_requested = true;
}

void VoiceCapturePipeline::close()
{
    requestStop();
    if (_resampler != nullptr) {
        esp_ae_rate_cvt_close(_resampler);
        _resampler = nullptr;
    }
    if (_recorder_open) {
        GetStopWatchAudioBackend().closeRecorder();
        _recorder_open = false;
    }
    if (_capture_owned) {
        GetResourceSupervisor().releaseCapture(RuntimeOwner::Codex);
        _capture_owned = false;
    }
    _resampler_output.clear();
    _microphone_pcm.clear();
    _resampler_output_capacity = 0;
    _open = false;
}

VoiceCaptureResult VoiceCapturePipeline::encodePendingBlock(
    std::array<uint8_t, kAdpcmBytesPerBlock>& adpcm,
    uint32_t& captured_samples,
    VoiceCaptureError& error
)
{
    ble_voice_probe::detail::MicrophonePcmBuffer::Block samples = {};
    if (!_microphone_pcm.take_complete_block(samples)) {
        return VoiceCaptureResult::Continue;
    }
    if (ble_voice_probe::encode_ima_adpcm_block(
            samples.data(),
            samples.size(),
            adpcm.data(),
            adpcm.size()) != kAdpcmBytesPerBlock) {
        error = VoiceCaptureError::EncoderFailed;
        return VoiceCaptureResult::Failed;
    }
    captured_samples = samples.size();
    return VoiceCaptureResult::BlockReady;
}
