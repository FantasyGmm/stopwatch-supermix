/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <map>
#include <memory>
#include <brookesia/hal_interface/device.hpp>
#include <brookesia/hal_interface/interfaces/audio/codec_player.hpp>
#include <brookesia/hal_interface/interfaces/audio/codec_recorder.hpp>

class StopWatchAudioBackend;

class StopWatchPlayer final : public esp_brookesia::hal::AudioCodecPlayerIface {
public:
    static constexpr const char* Name = "StopWatch:AudioPlayer";

    explicit StopWatchPlayer(StopWatchAudioBackend& backend);
    ~StopWatchPlayer() override;

    bool open(const Config& config) override;
    void close() override;
    bool set_volume(uint8_t volume) override;
    bool write_data(const uint8_t* data, size_t size) override;
    bool is_pa_on_off_supported() override;
    bool set_pa_on_off(bool on) override;
    bool is_pa_on() const override;

private:
    struct Impl;

    StopWatchAudioBackend& _backend;
    std::unique_ptr<Impl> _impl;
};

class StopWatchRecorder final : public esp_brookesia::hal::AudioCodecRecorderIface {
public:
    static constexpr const char* Name = "StopWatch:AudioRecorder";

    explicit StopWatchRecorder(StopWatchAudioBackend& backend);

    bool open() override;
    void close() override;
    bool read_data(uint8_t* data, size_t size) override;
    bool set_general_gain(float gain) override;
    bool set_channel_gains(const std::map<uint8_t, float>& gains) override;

private:
    StopWatchAudioBackend& _backend;
};

class StopWatchAudioDevice final : public esp_brookesia::hal::Device {
public:
    static constexpr const char* Name = "StopWatchAudio";

    StopWatchAudioDevice();

    bool probe() override;
    bool on_init() override;
    void on_deinit() override;
};

bool InitializeStopWatchAudioDevice();
bool StopWatchAudioInterfacesReady();
