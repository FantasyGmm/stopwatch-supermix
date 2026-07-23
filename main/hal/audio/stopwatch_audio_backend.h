/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <freertos/FreeRTOS.h>

class StopWatchAudioBackend {
public:
    static constexpr uint32_t sampleRate = 44100;
    static constexpr uint8_t bitsPerSample = 16;
    static constexpr uint8_t channelCount = 1;
    static constexpr std::size_t inputLevelCount = 15;
    using InputLevels = std::array<uint16_t, inputLevelCount>;

    StopWatchAudioBackend();
    ~StopWatchAudioBackend();
    StopWatchAudioBackend(const StopWatchAudioBackend&) = delete;
    StopWatchAudioBackend& operator=(const StopWatchAudioBackend&) = delete;

    bool initialize(void* i2c_bus);
    bool initialized() const;

    bool openRecorder(uint32_t sample_rate = sampleRate, uint8_t bits = bitsPerSample,
                      uint8_t channels = channelCount);
    void closeRecorder();
    std::size_t read(void* data, std::size_t bytes, TickType_t timeout = portMAX_DELAY);
    InputLevels inputLevels() const;

    bool openPlayer(uint32_t sample_rate = sampleRate, uint8_t bits = bitsPerSample,
                    uint8_t channels = channelCount);
    void closePlayer();
    std::size_t write(const void* data, std::size_t bytes, TickType_t timeout = portMAX_DELAY);
    bool resetPlayerPath();

    bool setVolume(uint8_t percent);
    int getVolume() const;
    bool setGeneralGain(float gain_db);
    bool runLifecycleDiagnostic(std::size_t cycles);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

StopWatchAudioBackend& GetStopWatchAudioBackend();
