/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "stopwatch_audio_device.h"
#include "hal/audio/stopwatch_audio_backend.h"
#include <algorithm>
#include <string>

StopWatchPlayer::StopWatchPlayer(StopWatchAudioBackend& backend) : _backend(backend)
{
}

bool StopWatchPlayer::open(const Config& config)
{
    return _backend.openPlayer(config.sample_rate, config.bits, config.channels);
}

void StopWatchPlayer::close()
{
    _backend.closePlayer();
}

bool StopWatchPlayer::set_volume(uint8_t volume)
{
    return _backend.setVolume(std::min<uint8_t>(volume, 100));
}

bool StopWatchPlayer::write_data(const uint8_t* data, size_t size)
{
    return _backend.write(data, size) == size;
}

bool StopWatchPlayer::is_pa_on_off_supported()
{
    return false;
}

bool StopWatchPlayer::set_pa_on_off(bool on)
{
    (void)on;
    return false;
}

bool StopWatchPlayer::is_pa_on() const
{
    return true;
}

StopWatchRecorder::StopWatchRecorder(StopWatchAudioBackend& backend)
    : AudioCodecRecorderIface({
          .bits          = StopWatchAudioBackend::bitsPerSample,
          .channels      = StopWatchAudioBackend::channelCount,
          .sample_rate   = StopWatchAudioBackend::sampleRate,
          .mic_layout    = "M",
          .general_gain  = 30.0f,
          .channel_gains = {},
      }),
      _backend(backend)
{
}

bool StopWatchRecorder::open()
{
    const auto& info = get_info();
    return _backend.openRecorder(info.sample_rate, info.bits, info.channels);
}

void StopWatchRecorder::close()
{
    _backend.closeRecorder();
}

bool StopWatchRecorder::read_data(uint8_t* data, size_t size)
{
    return _backend.read(data, size) == size;
}

bool StopWatchRecorder::set_general_gain(float gain)
{
    if (!_backend.setGeneralGain(gain)) {
        return false;
    }
    const_cast<Info&>(get_info()).general_gain = gain;
    return true;
}

bool StopWatchRecorder::set_channel_gains(const std::map<uint8_t, float>& gains)
{
    if (gains.empty()) {
        const_cast<Info&>(get_info()).channel_gains.clear();
        return true;
    }
    if (gains.size() != 1 || gains.begin()->first != 0 || !_backend.setGeneralGain(gains.begin()->second)) {
        return false;
    }
    auto& info         = const_cast<Info&>(get_info());
    info.channel_gains = gains;
    info.general_gain  = gains.begin()->second;
    return true;
}

StopWatchAudioDevice::StopWatchAudioDevice() : Device(std::string(Name))
{
}

bool StopWatchAudioDevice::probe()
{
    return GetStopWatchAudioBackend().initialized();
}

bool StopWatchAudioDevice::on_init()
{
    auto& backend = GetStopWatchAudioBackend();
    if (!backend.initialized()) {
        return false;
    }
    interfaces_.emplace(StopWatchPlayer::Name, std::make_shared<StopWatchPlayer>(backend));
    interfaces_.emplace(StopWatchRecorder::Name, std::make_shared<StopWatchRecorder>(backend));
    return true;
}

void StopWatchAudioDevice::on_deinit()
{
    GetStopWatchAudioBackend().closeRecorder();
    GetStopWatchAudioBackend().closePlayer();
    interfaces_.clear();
}

bool InitializeStopWatchAudioDevice()
{
    return esp_brookesia::hal::init_device(StopWatchAudioDevice::Name) && StopWatchAudioInterfacesReady();
}

bool StopWatchAudioInterfacesReady()
{
    auto device = esp_brookesia::hal::get_device_by_device_name(StopWatchAudioDevice::Name);
    if (device == nullptr) {
        return false;
    }
    return device->get_interface<esp_brookesia::hal::AudioCodecPlayerIface>(StopWatchPlayer::Name) != nullptr &&
           device->get_interface<esp_brookesia::hal::AudioCodecRecorderIface>(StopWatchRecorder::Name) != nullptr;
}

using StopWatchAudioDevicePlugin = StopWatchAudioDevice;
BROOKESIA_PLUGIN_REGISTER(esp_brookesia::hal::Device, StopWatchAudioDevicePlugin,
                          std::string(StopWatchAudioDevice::Name));
