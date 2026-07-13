/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "audio/stopwatch_audio_backend.h"
#include "utils/settings/settings.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <mooncake_log.h>
#include <esp_dsp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>

static const std::string_view _tag = "HAL-Audio";

static class AudioCodec {
public:
    static constexpr int sample_rate       = StopWatchAudioBackend::sampleRate;
    static constexpr int spectrum_fft_size = 512;
    static constexpr int spectrum_hop_size = 256;

    void init(i2c_master_bus_handle_t i2c_bus)
    {
        _silence_buffer.resize(sample_rate * 0.1);
        _silence_buffer.assign(_silence_buffer.size(), 0);
        _spectrum_init();
        if (!_backend.initialize(i2c_bus)) {
            mclog::tagError(_tag, "audio backend initialization failed");
        }
    }

    void updateSpectrum(Hal::AudioSpectrumFrame& frame)
    {
        if (_spectrum_available == false) {
            return;
        }

        if (_read_spectrum_hop() == false) {
            return;
        }

        if (_spectrum_samples_ready < spectrum_fft_size) {
            return;
        }

        _process_spectrum_frame(frame);
    }

    void setVolume(int volume)
    {
        _backend.setVolume(static_cast<uint8_t>(std::clamp(volume, 0, 100)));
    }

    int getVolume()
    {
        return _backend.getVolume();
    }

    void setMicGain(float gain)
    {
        _backend.setGeneralGain(gain);
    }

    void play(std::vector<int16_t>& data, bool async)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_backend.openPlayer()) {
            mclog::tagError(_tag, "player open failed");
            return;
        }
        if (async) {
            // Support interruption: overwrite data and notify task
            _audio_data = data;
            _is_playing = true;
            if (_task_handle == nullptr) {
                if (xTaskCreate([](void* obj) { static_cast<AudioCodec*>(obj)->_task_entry(); }, "audio_task",
                                4 * 1024, this, 5, &_task_handle) != pdPASS) {
                    _task_handle = nullptr;
                    _is_playing  = false;
                    _audio_data.clear();
                    _backend.closePlayer();
                    mclog::tagError(_tag, "audio play task creation failed");
                    return;
                }
            }
            xTaskNotifyGive(_task_handle);
        } else {
            if (_is_playing) {
                mclog::tagWarn(_tag, "audio is playing");
                return;
            }
            _write(data);
            _backend.closePlayer();
        }
    }

    void record(std::vector<int16_t>& data, uint16_t durationMs, float gain)
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_backend.openRecorder()) {
            mclog::tagError(_tag, "recorder open failed");
            data.clear();
            return;
        }
        _backend.setGeneralGain(gain);

        size_t sample_count = (size_t)(sample_rate * durationMs / 1000);
        size_t byte_size    = sample_count * sizeof(int16_t);

        data.resize(sample_count);

        if (_backend.read(data.data(), byte_size) != byte_size) {
            mclog::tagError(_tag, "record failed");
            data.clear();
        }
        _backend.closeRecorder();
    }

private:
    void _task_entry()
    {
        mclog::tagInfo(_tag, "start audio play task");
        std::vector<int16_t> current_data;

        while (true) {
            // Wait for play request
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            while (true) {
                // Fetch data safely
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    if (_audio_data.empty()) {
                        _is_playing = false;
                        break;
                    }
                    current_data = _audio_data;
                    _audio_data.clear();
                    _is_playing = true;
                }

                if (current_data.empty()) {
                    break;
                }

                size_t offset        = 0;
                size_t total_samples = current_data.size();
                bool interrupted     = false;
                // Chunk size in samples (e.g. 1024 bytes = 512 samples)
                const size_t CHUNK_SAMPLES = 512;

                while (offset < total_samples) {
                    // Check for interruption (new play request)
                    if (ulTaskNotifyTake(pdTRUE, 0) > 0) {
                        // mclog::tagInfo(_tag, "playback interrupted");
                        interrupted = true;
                        break;
                    }

                    size_t remain        = total_samples - offset;
                    size_t write_samples = (remain > CHUNK_SAMPLES) ? CHUNK_SAMPLES : remain;

                    _backend.write(&current_data[offset], write_samples * sizeof(int16_t));
                    offset += write_samples;
                }

                if (interrupted) {
                    // Stop current playback immediately and flush DMA
                    _backend.resetPlayerPath();
                    continue;
                }

                // Normal finish, play silence to avoid pop/waiting
                _backend.write(_silence_buffer.data(), _silence_buffer.size() * sizeof(int16_t));
            }

            std::lock_guard<std::mutex> lock(_mutex);
            if (!_audio_data.empty()) {
                continue;
            }
            _backend.closePlayer();
            _is_playing = false;
            _task_handle = nullptr;
            break;
        }

        mclog::tagInfo(_tag, "stop audio play task");
        vTaskDelete(nullptr);
    }

    void _write(const std::vector<int16_t>& data)
    {
        _backend.write(data.data(), data.size() * sizeof(int16_t));
        _backend.write(_silence_buffer.data(), _silence_buffer.size() * sizeof(int16_t));
    }

    void _spectrum_init()
    {
        esp_err_t ret = dsps_fft2r_init_fc32(nullptr, spectrum_fft_size);
        if (ret != ESP_OK) {
            mclog::tagError(_tag, "fft init failed: {}", ret);
            return;
        }

        dsps_wind_hann_f32(_spectrum_window.data(), spectrum_fft_size);

        constexpr int max_bin = spectrum_fft_size / 2;
        const float nyquist   = static_cast<float>(sample_rate) * 0.5f;
        const float min_hz    = static_cast<float>(sample_rate) / static_cast<float>(spectrum_fft_size);
        const float log_min   = std::log10(min_hz);
        const float log_max   = std::log10(nyquist);

        _band_bin_edges[0] = 1;
        for (std::size_t i = 1; i < Hal::AudioSpectrumFrame::bandCount; ++i) {
            float t            = static_cast<float>(i) / static_cast<float>(Hal::AudioSpectrumFrame::bandCount);
            float edge_hz      = std::pow(10.0f, log_min + (log_max - log_min) * t);
            int edge_bin       = static_cast<int>(std::lround(edge_hz * spectrum_fft_size / sample_rate));
            int min_edge       = _band_bin_edges[i - 1] + 1;
            int max_edge       = max_bin - static_cast<int>(Hal::AudioSpectrumFrame::bandCount - i);
            _band_bin_edges[i] = std::clamp(edge_bin, min_edge, max_edge);
        }
        _band_bin_edges[Hal::AudioSpectrumFrame::bandCount] = max_bin;
        _spectrum_available                                 = true;
    }

    bool _read_spectrum_hop()
    {
        std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);
        if (lock.owns_lock() == false) {
            return false;
        }

        if (!_backend.openRecorder()) {
            return false;
        }
        const std::size_t bytes = sizeof(int16_t) * spectrum_hop_size;
        if (_backend.read(_spectrum_pcm_hop.data(), bytes) != bytes) {
            _backend.closeRecorder();
            return false;
        }
        _backend.closeRecorder();

        std::move(_spectrum_time_domain.begin() + spectrum_hop_size, _spectrum_time_domain.end(),
                  _spectrum_time_domain.begin());

        for (int i = 0; i < spectrum_hop_size; ++i) {
            _spectrum_time_domain[spectrum_fft_size - spectrum_hop_size + i] =
                static_cast<float>(_spectrum_pcm_hop[i]) / 32768.0f;
        }

        _spectrum_samples_ready = std::min<std::size_t>(_spectrum_samples_ready + spectrum_hop_size, spectrum_fft_size);
        return true;
    }

    void _process_spectrum_frame(Hal::AudioSpectrumFrame& frame)
    {
        float mean = 0.0f;
        for (float sample : _spectrum_time_domain) {
            mean += sample;
        }
        mean /= static_cast<float>(spectrum_fft_size);

        for (int i = 0; i < spectrum_fft_size; ++i) {
            float sample                    = (_spectrum_time_domain[i] - mean) * _spectrum_window[i];
            _spectrum_fft_buffer[i * 2]     = sample;
            _spectrum_fft_buffer[i * 2 + 1] = 0.0f;
        }

        if (dsps_fft2r_fc32(_spectrum_fft_buffer.data(), spectrum_fft_size) != ESP_OK) {
            return;
        }
        if (dsps_bit_rev_fc32(_spectrum_fft_buffer.data(), spectrum_fft_size) != ESP_OK) {
            return;
        }

        float peak_bin_magnitude = 0.0f;
        int peak_bin_index       = 0;
        float top1               = 0.0f;
        float top2               = 0.0f;
        float top3               = 0.0f;

        for (std::size_t band = 0; band < Hal::AudioSpectrumFrame::bandCount; ++band) {
            int start_bin = _band_bin_edges[band];
            int end_bin   = _band_bin_edges[band + 1];
            float energy  = 0.0f;
            float peak    = 0.0f;
            int count     = 0;

            for (int bin = start_bin; bin < end_bin; ++bin) {
                float re  = _spectrum_fft_buffer[bin * 2];
                float im  = _spectrum_fft_buffer[bin * 2 + 1];
                float mag = std::sqrt(re * re + im * im) * (2.0f / static_cast<float>(spectrum_fft_size));
                if (mag > peak_bin_magnitude) {
                    peak_bin_magnitude = mag;
                    peak_bin_index     = bin;
                }
                peak = std::max(peak, mag);
                energy += mag * mag;
                ++count;
            }

            float rms = count > 0 ? std::sqrt(energy / static_cast<float>(count)) : 0.0f;
            float raw = rms * 0.48f + peak * 0.52f;
            float low_emphasis =
                1.12f - 0.22f * (static_cast<float>(band) / static_cast<float>(Hal::AudioSpectrumFrame::bandCount - 1));
            raw *= low_emphasis;

            float floor_alpha = raw < _spectrum_noise_floor[band] ? 0.45f : 0.004f;
            _spectrum_noise_floor[band] += (raw - _spectrum_noise_floor[band]) * floor_alpha;
            raw                       = std::max(raw - (_spectrum_noise_floor[band] * 2.20f + 0.0018f), 0.0f);
            _spectrum_raw_bands[band] = raw;

            if (raw >= top1) {
                top3 = top2;
                top2 = top1;
                top1 = raw;
            } else if (raw >= top2) {
                top3 = top2;
                top2 = raw;
            } else if (raw > top3) {
                top3 = raw;
            }
        }

        float frame_reference = std::max(top1 * 0.80f + top2 * 0.14f + top3 * 0.06f, 0.0015f);
        float norm_alpha      = frame_reference > _spectrum_normalization_level ? 0.44f : 0.16f;
        _spectrum_normalization_level += (frame_reference - _spectrum_normalization_level) * norm_alpha;
        _spectrum_normalization_level = std::clamp(_spectrum_normalization_level, 0.0015f, 1.0f);

        if (peak_bin_magnitude > 0.0f) {
            float refined_bin = static_cast<float>(peak_bin_index);
            if (peak_bin_index > 1 && peak_bin_index < (spectrum_fft_size / 2 - 1)) {
                float left_re      = _spectrum_fft_buffer[(peak_bin_index - 1) * 2];
                float left_im      = _spectrum_fft_buffer[(peak_bin_index - 1) * 2 + 1];
                float right_re     = _spectrum_fft_buffer[(peak_bin_index + 1) * 2];
                float right_im     = _spectrum_fft_buffer[(peak_bin_index + 1) * 2 + 1];
                float center_power = peak_bin_magnitude * peak_bin_magnitude;
                float left_power   = left_re * left_re + left_im * left_im;
                float right_power  = right_re * right_re + right_im * right_im;
                float denom        = left_power - 2.0f * center_power + right_power;

                if (std::fabs(denom) > 1e-9f) {
                    float offset = 0.5f * (left_power - right_power) / denom;
                    refined_bin += std::clamp(offset, -0.5f, 0.5f);
                }
            }
            frame.peakFrequencyHz =
                refined_bin * static_cast<float>(sample_rate) / static_cast<float>(spectrum_fft_size);
        } else {
            frame.peakFrequencyHz = 0.0f;
        }

        for (std::size_t band = 0; band < Hal::AudioSpectrumFrame::bandCount; ++band) {
            float ratio      = _spectrum_raw_bands[band] / _spectrum_normalization_level;
            float normalized = std::clamp(std::pow(ratio, 0.55f), 0.0f, 1.0f);
            if (normalized < 0.035f) {
                normalized = 0.0f;
            }

            float smooth_alpha = normalized > _spectrum_smoothed_bands[band] ? 0.82f : 0.40f;
            _spectrum_smoothed_bands[band] += (normalized - _spectrum_smoothed_bands[band]) * smooth_alpha;
            frame.bands[band] = std::clamp(_spectrum_smoothed_bands[band], 0.0f, 1.0f);
        }
    }

    StopWatchAudioBackend& _backend = GetStopWatchAudioBackend();
    TaskHandle_t _task_handle       = nullptr;
    std::mutex _mutex;
    std::vector<int16_t> _audio_data;
    std::vector<int16_t> _silence_buffer;
    std::array<int16_t, spectrum_hop_size> _spectrum_pcm_hop                       = {};
    std::array<float, spectrum_fft_size> _spectrum_time_domain                     = {};
    std::array<float, spectrum_fft_size> _spectrum_window                          = {};
    std::array<float, spectrum_fft_size * 2> _spectrum_fft_buffer                  = {};
    std::array<float, Hal::AudioSpectrumFrame::bandCount> _spectrum_raw_bands      = {};
    std::array<float, Hal::AudioSpectrumFrame::bandCount> _spectrum_smoothed_bands = {};
    std::array<float, Hal::AudioSpectrumFrame::bandCount> _spectrum_noise_floor    = {};
    std::array<int, Hal::AudioSpectrumFrame::bandCount + 1> _band_bin_edges        = {};
    std::size_t _spectrum_samples_ready                                            = 0;
    float _spectrum_normalization_level                                            = 0.03f;
    bool _spectrum_available                                                       = false;
    bool _is_playing                                                               = false;
} _audio_codec;

void Hal::audio_init()
{
    mclog::tagInfo(_tag, "init");

    _audio_codec.init(i2c_bus_get_internal_bus_handle(_i2c_bus));

    ioe_speaker_enable(true);

    // Load volume from settings
    setSpeakerVolume(getSpeakerVolume(true), false);
}

void Hal::setSpeakerVolume(int volume, bool saveToSettings)
{
    _spk_volume = volume;
    _spk_volume = uitk::clamp(_spk_volume, 0, 100);

    mclog::tagInfo(_tag, "set speaker volume to {}", _spk_volume);
    _audio_codec.setVolume(_spk_volume);

    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetInt("spk_vol", _spk_volume);
        mclog::tagInfo(_tag, "volume saved to settings: {}", _spk_volume);
    }
}

int Hal::getSpeakerVolume(bool loadFromSettings)
{
    _spk_volume = _audio_codec.getVolume();

    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        _spk_volume = settings.GetInt("spk_vol", 80);
        _spk_volume = uitk::clamp(_spk_volume, 0, 100);
        mclog::tagInfo(_tag, "volume loaded from settings: {}", _spk_volume);
    }

    return _spk_volume;
}

void Hal::audioRecord(std::vector<int16_t>& data, uint16_t durationMs, float gain)
{
    _audio_codec.record(data, durationMs, gain);
}

void Hal::audioPlay(std::vector<int16_t>& data, bool async)
{
    _audio_codec.play(data, async);
}

int Hal::getAudioSampleRate()
{
    return _audio_codec.sample_rate;
}

StopWatchAudioBackend& Hal::getAudioBackend()
{
    return GetStopWatchAudioBackend();
}

void Hal::updateAudioSpectrum()
{
    _audio_codec.updateSpectrum(_audio_spectrum);
}

namespace {

extern const uint8_t _boot_sfx_start[] asm("_binary_boot_sfx_bin_start");
extern const uint8_t _boot_sfx_end[] asm("_binary_boot_sfx_bin_end");

}  // namespace

void Hal::playBootSfx()
{
    mclog::tagInfo(_tag, "play boot sfx");

    const std::size_t byte_count = _boot_sfx_end - _boot_sfx_start;
    if (byte_count == 0 || (byte_count % sizeof(int16_t)) != 0) {
        mclog::tagError(_tag, "boot sfx binary has invalid size: {}", byte_count);
        return;
    }

    const auto* samples     = reinterpret_cast<const int16_t*>(_boot_sfx_start);
    const std::size_t count = byte_count / sizeof(int16_t);
    std::vector<int16_t> pcm(samples, samples + count);

    audioPlay(pcm, true);
}
