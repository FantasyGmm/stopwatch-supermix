/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "stopwatch_audio_backend.h"
#include "audio_backend_lifecycle.h"
#include <driver/i2s_std.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <mooncake_log.h>
#include <mutex>
#include <string_view>

namespace {

constexpr std::string_view Tag = "AudioBackend";
constexpr i2s_port_t I2sPort   = I2S_NUM_0;
constexpr gpio_num_t MclkPin   = GPIO_NUM_18;
constexpr gpio_num_t BclkPin   = GPIO_NUM_17;
constexpr gpio_num_t DataInPin = GPIO_NUM_16;
constexpr gpio_num_t LrckPin   = GPIO_NUM_15;
constexpr gpio_num_t DataOutPin = GPIO_NUM_21;

bool validFormat(uint32_t sample_rate, uint8_t bits, uint8_t channels)
{
    return sample_rate == StopWatchAudioBackend::sampleRate && bits == StopWatchAudioBackend::bitsPerSample &&
           channels == StopWatchAudioBackend::channelCount;
}

}  // namespace

struct StopWatchAudioBackend::Impl {
    mutable std::mutex mutex;
    AudioBackendLifecycle lifecycle;
    i2s_chan_handle_t tx_handle              = nullptr;
    i2s_chan_handle_t rx_handle              = nullptr;
    esp_codec_dev_handle_t codec_dev         = nullptr;
    const audio_codec_data_if_t* data_if     = nullptr;
    const audio_codec_ctrl_if_t* ctrl_if     = nullptr;
    const audio_codec_gpio_if_t* gpio_if     = nullptr;
    const audio_codec_if_t* codec_if         = nullptr;
    bool initialized                         = false;
};

StopWatchAudioBackend::StopWatchAudioBackend() : _impl(std::make_unique<Impl>())
{
}

StopWatchAudioBackend::~StopWatchAudioBackend() = default;

StopWatchAudioBackend& GetStopWatchAudioBackend()
{
    static StopWatchAudioBackend backend;
    return backend;
}

bool StopWatchAudioBackend::initialize(void* i2c_bus)
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (_impl->initialized) {
        return true;
    }
    if (i2c_bus == nullptr) {
        mclog::tagError(Tag, "cannot initialize without I2C bus");
        return false;
    }

    mclog::tagInfo(Tag, "initializing codec and I2S once");
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2sPort, I2S_ROLE_MASTER);
    i2s_std_config_t std_cfg   = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = MclkPin,
                .bclk = BclkPin,
                .ws   = LrckPin,
                .dout = DataOutPin,
                .din  = DataInPin,
            },
    };

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &_impl->tx_handle, &_impl->rx_handle));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(_impl->tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(_impl->rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(_impl->tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(_impl->rx_handle));

    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = _impl->rx_handle,
        .tx_handle = _impl->tx_handle,
    };
    _impl->data_if = audio_codec_new_i2s_data(&i2s_cfg);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr       = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    _impl->ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    _impl->gpio_if = audio_codec_new_gpio();

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if     = _impl->ctrl_if,
        .gpio_if     = _impl->gpio_if,
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin      = GPIO_NUM_NC,
        .pa_reverted = false,
        .use_mclk    = true,
    };
    _impl->codec_if = es8311_codec_new(&es8311_cfg);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = _impl->codec_if,
        .data_if  = _impl->data_if,
    };
    _impl->codec_dev = esp_codec_dev_new(&dev_cfg);

    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = bitsPerSample,
        .channel         = channelCount,
        .sample_rate     = sampleRate,
    };
    if (_impl->codec_dev == nullptr || esp_codec_dev_open(_impl->codec_dev, &sample_info) != ESP_CODEC_DEV_OK) {
        mclog::tagError(Tag, "codec open failed");
        return false;
    }

    esp_codec_dev_set_in_gain(_impl->codec_dev, 30.0f);
    ESP_ERROR_CHECK(i2s_channel_disable(_impl->rx_handle));
    ESP_ERROR_CHECK(i2s_channel_disable(_impl->tx_handle));
    _impl->initialized = true;
    return true;
}

bool StopWatchAudioBackend::initialized() const
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->initialized;
}

bool StopWatchAudioBackend::openRecorder(uint32_t sample_rate, uint8_t bits, uint8_t channels)
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (!_impl->initialized || !validFormat(sample_rate, bits, channels)) {
        return false;
    }
    if (_impl->lifecycle.openRecorder() == AudioOpenResult::AlreadyOpen) {
        return true;
    }
    if (i2s_channel_enable(_impl->rx_handle) != ESP_OK) {
        _impl->lifecycle.closeRecorder();
        return false;
    }
    return true;
}

void StopWatchAudioBackend::closeRecorder()
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (_impl->lifecycle.closeRecorder() == AudioCloseResult::Closed) {
        i2s_channel_disable(_impl->rx_handle);
    }
}

std::size_t StopWatchAudioBackend::read(void* data, std::size_t bytes, TickType_t timeout)
{
    (void)timeout;
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (!_impl->lifecycle.recorderOpen() || data == nullptr || bytes == 0) {
        return 0;
    }
    return esp_codec_dev_read(_impl->codec_dev, data, bytes) == ESP_CODEC_DEV_OK ? bytes : 0;
}

bool StopWatchAudioBackend::openPlayer(uint32_t sample_rate, uint8_t bits, uint8_t channels)
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (!_impl->initialized || !validFormat(sample_rate, bits, channels)) {
        return false;
    }
    if (_impl->lifecycle.openPlayer() == AudioOpenResult::AlreadyOpen) {
        return true;
    }
    if (i2s_channel_enable(_impl->tx_handle) != ESP_OK) {
        _impl->lifecycle.closePlayer();
        return false;
    }
    return true;
}

void StopWatchAudioBackend::closePlayer()
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (_impl->lifecycle.closePlayer() == AudioCloseResult::Closed) {
        i2s_channel_disable(_impl->tx_handle);
    }
}

std::size_t StopWatchAudioBackend::write(const void* data, std::size_t bytes, TickType_t timeout)
{
    (void)timeout;
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (!_impl->lifecycle.playerOpen() || data == nullptr || bytes == 0) {
        return 0;
    }
    return esp_codec_dev_write(_impl->codec_dev, const_cast<void*>(data), bytes) == ESP_CODEC_DEV_OK ? bytes : 0;
}

bool StopWatchAudioBackend::resetPlayerPath()
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (!_impl->lifecycle.playerOpen()) {
        return false;
    }
    return i2s_channel_disable(_impl->tx_handle) == ESP_OK && i2s_channel_enable(_impl->tx_handle) == ESP_OK;
}

bool StopWatchAudioBackend::setVolume(uint8_t percent)
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->initialized && esp_codec_dev_set_out_vol(_impl->codec_dev, percent) == ESP_CODEC_DEV_OK;
}

int StopWatchAudioBackend::getVolume() const
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    int volume = 0;
    if (_impl->initialized) {
        esp_codec_dev_get_out_vol(_impl->codec_dev, &volume);
    }
    return volume;
}

bool StopWatchAudioBackend::setGeneralGain(float gain_db)
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->initialized && esp_codec_dev_set_in_gain(_impl->codec_dev, gain_db) == ESP_CODEC_DEV_OK;
}

bool StopWatchAudioBackend::runLifecycleDiagnostic(std::size_t cycles)
{
    for (std::size_t index = 0; index < cycles; ++index) {
        if (!openRecorder()) {
            mclog::tagError(Tag, "recorder lifecycle diagnostic failed at cycle {}", index);
            return false;
        }
        closeRecorder();

        if (!openPlayer()) {
            mclog::tagError(Tag, "player lifecycle diagnostic failed at cycle {}", index);
            return false;
        }
        closePlayer();
    }
    mclog::tagInfo(Tag, "audio lifecycle diagnostic passed {} cycles", cycles);
    return true;
}
