#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

template <size_t CapacityFrames>
class VoiceAudioRing
{
    static_assert(CapacityFrames > 0, "VoiceAudioRing capacity must be nonzero");

public:
    size_t writeMono16kAsStereo48k(const int16_t* samples, size_t count)
    {
        if (samples == nullptr || count == 0) {
            return 0;
        }

        const uint64_t write_frame = write_frame_.load(std::memory_order_relaxed);
        const uint64_t read_frame = read_frame_.load(std::memory_order_acquire);
        const size_t available_frames =
            CapacityFrames - static_cast<size_t>(write_frame - read_frame);
        const size_t accepted_samples = std::min(count, available_frames / 3);

        for (size_t sample_index = 0; sample_index < accepted_samples; ++sample_index) {
            for (size_t duplicate = 0; duplicate < 3; ++duplicate) {
                const size_t frame =
                    static_cast<size_t>(write_frame + sample_index * 3 + duplicate) %
                    CapacityFrames;
                samples_[frame * 2] = samples[sample_index];
                samples_[frame * 2 + 1] = samples[sample_index];
            }
        }

        const size_t written_frames = accepted_samples * 3;
        dropped_frames_.fetch_add(
            static_cast<uint64_t>((count - accepted_samples) * 3),
            std::memory_order_relaxed);
        write_frame_.store(write_frame + written_frames, std::memory_order_release);
        return written_frames;
    }

    size_t readStereo(int16_t* interleaved_stereo, size_t frame_count)
    {
        if (interleaved_stereo == nullptr || frame_count == 0) {
            return 0;
        }

        const uint64_t read_frame = read_frame_.load(std::memory_order_relaxed);
        const uint64_t write_frame = write_frame_.load(std::memory_order_acquire);
        const size_t frames_to_read = std::min(
            frame_count,
            static_cast<size_t>(write_frame - read_frame));

        for (size_t index = 0; index < frames_to_read; ++index) {
            const size_t frame = static_cast<size_t>(read_frame + index) % CapacityFrames;
            interleaved_stereo[index * 2] = samples_[frame * 2];
            interleaved_stereo[index * 2 + 1] = samples_[frame * 2 + 1];
        }

        read_frame_.store(read_frame + frames_to_read, std::memory_order_release);
        return frames_to_read;
    }

    size_t queuedFrames() const
    {
        const uint64_t read_frame = read_frame_.load(std::memory_order_acquire);
        const uint64_t write_frame = write_frame_.load(std::memory_order_acquire);
        return std::min(
            CapacityFrames,
            static_cast<size_t>(write_frame - read_frame));
    }

    uint64_t droppedFrames() const
    {
        return dropped_frames_.load(std::memory_order_relaxed);
    }

    void clear()
    {
        const uint64_t write_frame = write_frame_.load(std::memory_order_acquire);
        read_frame_.store(write_frame, std::memory_order_release);
    }

private:
    std::array<int16_t, CapacityFrames * 2> samples_;
    std::atomic<uint64_t> read_frame_{0};
    std::atomic<uint64_t> write_frame_{0};
    std::atomic<uint64_t> dropped_frames_{0};
};
