/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "stopwatch_audio_device.h"
#include "audio_jitter_buffer_policy.h"
#include "hal/audio/stopwatch_audio_backend.h"
#include <algorithm>
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/idf_additions.h>
#include <freertos/ringbuf.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mooncake_log.h>
#include <mutex>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view PlayerTag = "AssistantAudioOut";
constexpr size_t OutputBufferBytes = 64 * 1024;
constexpr TickType_t OutputReceivePoll = pdMS_TO_TICKS(10);
constexpr TickType_t OutputEnqueueTimeout = pdMS_TO_TICKS(20);
constexpr TickType_t OutputStopTimeout = pdMS_TO_TICKS(1500);
constexpr uint32_t OutputTaskStackBytes = 4 * 1024;
constexpr UBaseType_t OutputTaskPriority = 6;
constexpr int64_t PcmBytesPerSecond = StopWatchAudioBackend::sampleRate *
                                      StopWatchAudioBackend::channelCount *
                                      (StopWatchAudioBackend::bitsPerSample / 8);
constexpr int64_t StartBufferDurationMs = 180;
constexpr size_t StartBufferBytes = PcmBytesPerSecond * StartBufferDurationMs / 1000;
constexpr int64_t MaxBufferWaitUs = 220 * 1000;
constexpr TickType_t BufferingPoll = pdMS_TO_TICKS(2);

}  // namespace

struct StopWatchPlayer::Impl {
    explicit Impl(StopWatchAudioBackend& audio_backend) : backend(audio_backend)
    {
    }

    ~Impl()
    {
        stop();
    }

    bool start()
    {
        if (task != nullptr) {
            return true;
        }
        ringbuffer = xRingbufferCreateWithCaps(
            OutputBufferBytes, RINGBUF_TYPE_NOSPLIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        stopped = xSemaphoreCreateBinary();
        if (ringbuffer == nullptr || stopped == nullptr) {
            cleanupResources();
            return false;
        }

        stop_requested.store(false);
        const BaseType_t created = xTaskCreateWithCaps(
            taskEntry, "assistant_audio_out", OutputTaskStackBytes, this, OutputTaskPriority,
            &task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (created != pdPASS) {
            task = nullptr;
            cleanupResources();
            return false;
        }
        mclog::tagInfo(PlayerTag, "output worker started with {} byte PSRAM buffer",
                       OutputBufferBytes);
        return true;
    }

    void stop()
    {
        if (task != nullptr) {
            stop_requested.store(true);
            xTaskNotifyGive(task);
            if (xSemaphoreTake(stopped, OutputStopTimeout) != pdTRUE) {
                mclog::tagError(PlayerTag, "output worker stop timed out");
                xSemaphoreTake(stopped, portMAX_DELAY);
            }
            task = nullptr;
        }
        cleanupResources();
    }

    bool enqueue(const uint8_t* data, size_t size)
    {
        if (ringbuffer == nullptr || stop_requested.load() || data == nullptr || size == 0) {
            return false;
        }
        const int64_t now_us = esp_timer_get_time();
        {
            std::lock_guard<std::mutex> lock(policy_mutex);
            jitter_policy.onQueued(size, now_us);
        }
        if (xRingbufferSend(ringbuffer, data, size, OutputEnqueueTimeout) != pdTRUE) {
            {
                std::lock_guard<std::mutex> lock(policy_mutex);
                jitter_policy.onConsumed(size);
            }
            mclog::tagError(PlayerTag, "PCM queue full: rejected {} bytes", size);
            return false;
        }
        return true;
    }

    static void taskEntry(void* context)
    {
        static_cast<Impl*>(context)->run();
    }

    void run()
    {
        while (!stop_requested.load()) {
            {
                std::lock_guard<std::mutex> lock(policy_mutex);
                if (!jitter_policy.readyToPlay(esp_timer_get_time())) {
                    vTaskDelay(BufferingPoll);
                    continue;
                }
            }
            size_t size = 0;
            void* item = xRingbufferReceive(ringbuffer, &size, OutputReceivePoll);
            if (item == nullptr) {
                continue;
            }
            if (!stop_requested.load() && backend.write(item, size) != size) {
                mclog::tagError(PlayerTag, "I2S output failed for {} bytes", size);
            }
            {
                std::lock_guard<std::mutex> lock(policy_mutex);
                jitter_policy.onConsumed(size);
            }
            vRingbufferReturnItem(ringbuffer, item);
        }
        drain();
        xSemaphoreGive(stopped);
        mclog::tagInfo(PlayerTag, "output worker stopped");
        vTaskDeleteWithCaps(nullptr);
    }

    void drain()
    {
        if (ringbuffer == nullptr) {
            return;
        }
        size_t size = 0;
        while (void* item = xRingbufferReceive(ringbuffer, &size, 0)) {
            vRingbufferReturnItem(ringbuffer, item);
        }
    }

    void cleanupResources()
    {
        if (ringbuffer != nullptr) {
            drain();
            vRingbufferDeleteWithCaps(ringbuffer);
            ringbuffer = nullptr;
        }
        if (stopped != nullptr) {
            vSemaphoreDelete(stopped);
            stopped = nullptr;
        }
        std::lock_guard<std::mutex> lock(policy_mutex);
        jitter_policy.reset();
    }

    StopWatchAudioBackend& backend;
    RingbufHandle_t ringbuffer = nullptr;
    SemaphoreHandle_t stopped = nullptr;
    TaskHandle_t task = nullptr;
    std::atomic<bool> stop_requested{false};
    std::mutex policy_mutex;
    AudioJitterBufferPolicy jitter_policy{StartBufferBytes, MaxBufferWaitUs};
};

StopWatchPlayer::StopWatchPlayer(StopWatchAudioBackend& backend)
    : _backend(backend), _impl(std::make_unique<Impl>(backend))
{
}

StopWatchPlayer::~StopWatchPlayer() = default;

bool StopWatchPlayer::open(const Config& config)
{
    if (!_backend.openPlayer(config.sample_rate, config.bits, config.channels)) {
        return false;
    }
    if (!_impl->start()) {
        _backend.closePlayer();
        return false;
    }
    return true;
}

void StopWatchPlayer::close()
{
    _impl->stop();
    _backend.closePlayer();
}

bool StopWatchPlayer::set_volume(uint8_t volume)
{
    return _backend.setVolume(std::min<uint8_t>(volume, 100));
}

bool StopWatchPlayer::write_data(const uint8_t* data, size_t size)
{
    return _impl->enqueue(data, size);
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
