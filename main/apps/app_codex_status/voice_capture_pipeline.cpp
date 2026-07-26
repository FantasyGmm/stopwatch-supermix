#include "voice_capture_pipeline.h"

#ifndef STOPWATCH_HOST_TEST
#include "ima_adpcm_codec.h"

#include <hal/audio/stopwatch_audio_backend.h>
#include <esp_ae_rate_cvt.h>
#include <freertos/FreeRTOS.h>
#include <runtime/resource_supervisor.h>
#endif

namespace {

constexpr uint32_t kMaxResamplerOutputSamples = 512;
std::atomic<VoiceCapturePipeline*> g_capture_lease{nullptr};

#ifndef STOPWATCH_HOST_TEST
constexpr uint32_t kCaptureRate = 44100;
constexpr uint32_t kOutputRate = 16000;

class ProductionVoiceCaptureBackend final : public VoiceCaptureBackend {
public:
    bool acquireCapture() override
    {
        return GetResourceSupervisor().acquireCapture(RuntimeOwner::Codex) ==
               AcquireResult::Granted;
    }

    void releaseCapture() override
    {
        GetResourceSupervisor().releaseCapture(RuntimeOwner::Codex);
    }

    bool openRecorder() override
    {
        return GetStopWatchAudioBackend().openRecorder(kCaptureRate, 16, 1);
    }

    void closeRecorder() override
    {
        GetStopWatchAudioBackend().closeRecorder();
    }

    size_t readRecorder(
        int16_t* samples,
        size_t sample_count,
        uint32_t timeout_ms) override
    {
        return GetStopWatchAudioBackend().read(
            samples,
            sample_count * sizeof(*samples),
            pdMS_TO_TICKS(timeout_ms));
    }

    bool openResampler(void*& handle) override
    {
        esp_ae_rate_cvt_cfg_t config = {};
        config.src_rate = kCaptureRate;
        config.dest_rate = kOutputRate;
        config.channel = 1;
        config.bits_per_sample = 16;
        config.complexity = 3;
        config.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY;
        return esp_ae_rate_cvt_open(&config, &handle) == ESP_AE_ERR_OK &&
               handle != nullptr;
    }

    bool maxOutputSamples(
        void* handle,
        size_t input_samples,
        uint32_t& output_samples) override
    {
        return esp_ae_rate_cvt_get_max_out_sample_num(
                   handle, input_samples, &output_samples) == ESP_AE_ERR_OK;
    }

    bool resample(
        void* handle,
        const int16_t* input,
        size_t input_samples,
        int16_t* output,
        uint32_t& output_samples) override
    {
        return esp_ae_rate_cvt_process(
                   handle,
                   static_cast<esp_ae_sample_t>(const_cast<int16_t*>(input)),
                   input_samples,
                   static_cast<esp_ae_sample_t>(output),
                   &output_samples) == ESP_AE_ERR_OK;
    }

    void closeResampler(void* handle) override
    {
        esp_ae_rate_cvt_close(handle);
    }

    size_t encodeAdpcm(
        const int16_t* samples,
        size_t sample_count,
        uint8_t* output,
        size_t capacity) override
    {
        return ble_voice_probe::encode_ima_adpcm_block(
            samples, sample_count, output, capacity);
    }
};

VoiceCaptureBackend& productionBackend()
{
    static ProductionVoiceCaptureBackend backend;
    return backend;
}
#endif

}  // namespace

#ifndef STOPWATCH_HOST_TEST
VoiceCapturePipeline::VoiceCapturePipeline()
    : VoiceCapturePipeline(productionBackend())
{
}
#endif

VoiceCapturePipeline::VoiceCapturePipeline(VoiceCaptureBackend& backend)
    : _backend(&backend)
{
}

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

    VoiceCapturePipeline* expected = nullptr;
    if (!g_capture_lease.compare_exchange_strong(expected, this)) {
        error = VoiceCaptureError::CaptureBusy;
        _stop_requested = true;
        return false;
    }
    _global_lease_owned = true;

    if (!_backend->acquireCapture()) {
        error = VoiceCaptureError::CaptureBusy;
        close();
        return false;
    }
    _capture_owned = true;

    if (!_backend->openRecorder()) {
        error = VoiceCaptureError::RecorderOpenFailed;
        close();
        return false;
    }
    _recorder_open = true;

    if (!_backend->openResampler(_resampler) || _resampler == nullptr) {
        error = VoiceCaptureError::ResamplerFailed;
        close();
        return false;
    }

    uint32_t output_capacity = 0;
    if (!_backend->maxOutputSamples(
            _resampler, kCaptureInputSamples, output_capacity) ||
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
    if (_backend->readRecorder(
            _capture_input.data(),
            _capture_input.size(),
            kCaptureReadTimeoutMs) != expected_bytes) {
        if (_stop_requested.load()) {
            return VoiceCaptureResult::Stopped;
        }
        error = VoiceCaptureError::RecorderOpenFailed;
        return VoiceCaptureResult::Failed;
    }
    uint32_t output_samples = _resampler_output_capacity;
    if (!_backend->resample(
            _resampler,
            _capture_input.data(),
            _capture_input.size(),
            _resampler_output.data(),
            output_samples) ||
        output_samples > _resampler_output_capacity) {
        error = VoiceCaptureError::ResamplerFailed;
        return VoiceCaptureResult::Failed;
    }

    _microphone_pcm.append(_resampler_output.data(), output_samples);
    result = encodePendingBlock(adpcm, captured_samples, error);
    if (result != VoiceCaptureResult::Continue) {
        return result;
    }
    return _stop_requested.load() ? VoiceCaptureResult::Stopped
                                  : VoiceCaptureResult::Continue;
}

void VoiceCapturePipeline::requestStop()
{
    _stop_requested = true;
}

void VoiceCapturePipeline::close()
{
    requestStop();
    if (_resampler != nullptr) {
        _backend->closeResampler(_resampler);
        _resampler = nullptr;
    }
    if (_recorder_open) {
        _backend->closeRecorder();
        _recorder_open = false;
    }
    if (_capture_owned) {
        _backend->releaseCapture();
        _capture_owned = false;
    }
    if (_global_lease_owned) {
        VoiceCapturePipeline* expected = this;
        (void)g_capture_lease.compare_exchange_strong(expected, nullptr);
        _global_lease_owned = false;
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
    if (_backend->encodeAdpcm(
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
