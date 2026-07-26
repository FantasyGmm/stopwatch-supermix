#include "main/apps/app_codex_status/voice_capture_pipeline.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

namespace {

class TestBackend final : public VoiceCaptureBackend {
public:
    bool acquireCapture() override
    {
        ++acquire_calls;
        return acquire_ok;
    }

    void releaseCapture() override
    {
        ++release_calls;
    }

    bool openRecorder() override
    {
        ++recorder_open_calls;
        return recorder_open_ok;
    }

    void closeRecorder() override
    {
        ++recorder_close_calls;
    }

    size_t readRecorder(
        int16_t* samples,
        size_t sample_count,
        uint32_t timeout_ms) override
    {
        ++read_calls;
        last_read_timeout_ms = timeout_ms;
        {
            std::unique_lock<std::mutex> lock(read_mutex);
            read_entered = true;
            read_condition.notify_all();
            if (block_read) {
                read_condition.wait_for(
                    lock,
                    std::chrono::milliseconds(timeout_ms),
                    [this] { return release_read; });
            }
        }
        if (!read_ok) {
            return 0;
        }
        std::fill_n(samples, sample_count, static_cast<int16_t>(1200));
        return sample_count * sizeof(*samples);
    }

    bool openResampler(void*& handle) override
    {
        ++resampler_open_calls;
        if (!resampler_open_ok) {
            handle = nullptr;
            return false;
        }
        handle = this;
        return true;
    }

    bool maxOutputSamples(
        void*,
        size_t,
        uint32_t& output_samples) override
    {
        ++resampler_capacity_calls;
        output_samples = resampler_capacity;
        return resampler_capacity_ok;
    }

    bool resample(
        void*,
        const int16_t*,
        size_t,
        int16_t* output,
        uint32_t& output_samples) override
    {
        ++resample_calls;
        if (!resample_ok) {
            return false;
        }
        output_samples = std::min(output_samples, resampled_samples);
        std::fill_n(output, output_samples, static_cast<int16_t>(600));
        return true;
    }

    void closeResampler(void*) override
    {
        ++resampler_close_calls;
    }

    size_t encodeAdpcm(
        const int16_t*,
        size_t,
        uint8_t* output,
        size_t capacity) override
    {
        ++encode_calls;
        if (!encode_ok) {
            return 0;
        }
        std::fill_n(output, capacity, static_cast<uint8_t>(0x5a));
        return capacity;
    }

    void waitUntilReadEntered()
    {
        std::unique_lock<std::mutex> lock(read_mutex);
        const bool entered = read_condition.wait_for(
            lock,
            std::chrono::seconds(1),
            [this] { return read_entered; });
        assert(entered);
    }

    void unblockRead()
    {
        std::lock_guard<std::mutex> lock(read_mutex);
        release_read = true;
        read_condition.notify_all();
    }

    bool acquire_ok = true;
    bool recorder_open_ok = true;
    bool resampler_open_ok = true;
    bool resampler_capacity_ok = true;
    bool read_ok = true;
    bool resample_ok = true;
    bool encode_ok = true;
    bool block_read = false;
    uint32_t resampler_capacity = 512;
    uint32_t resampled_samples = VoiceCapturePipeline::kOutputSamplesPerBlock;

    int acquire_calls = 0;
    int release_calls = 0;
    int recorder_open_calls = 0;
    int recorder_close_calls = 0;
    int resampler_open_calls = 0;
    int resampler_capacity_calls = 0;
    int resampler_close_calls = 0;
    int read_calls = 0;
    int resample_calls = 0;
    int encode_calls = 0;
    uint32_t last_read_timeout_ms = 0;

private:
    std::mutex read_mutex;
    std::condition_variable read_condition;
    bool read_entered = false;
    bool release_read = false;
};

void assertClosedExactlyOnce(const TestBackend& backend)
{
    assert(backend.release_calls == 1);
    assert(backend.recorder_close_calls == 1);
    assert(backend.resampler_close_calls == 1);
}

void testSuccessfulOpenCaptureClose()
{
    TestBackend backend;
    VoiceCapturePipeline pipeline(backend);
    VoiceCaptureError error = VoiceCaptureError::EncoderFailed;
    assert(pipeline.open(error));
    assert(error == VoiceCaptureError::None);

    std::array<uint8_t, VoiceCapturePipeline::kAdpcmBytesPerBlock> adpcm = {};
    uint32_t captured_samples = 0;
    assert(pipeline.captureBlock(adpcm, captured_samples, error) ==
           VoiceCaptureResult::BlockReady);
    assert(captured_samples == VoiceCapturePipeline::kOutputSamplesPerBlock);
    assert(error == VoiceCaptureError::None);
    assert(backend.last_read_timeout_ms ==
           VoiceCapturePipeline::kCaptureReadTimeoutMs);

    pipeline.close();
    assertClosedExactlyOnce(backend);
}

void testPartialOpenFailuresRollbackExactlyOnce()
{
    {
        TestBackend backend;
        backend.acquire_ok = false;
        VoiceCapturePipeline pipeline(backend);
        VoiceCaptureError error = VoiceCaptureError::None;
        assert(!pipeline.open(error));
        assert(error == VoiceCaptureError::CaptureBusy);
        assert(backend.release_calls == 0);
        pipeline.close();
        assert(backend.release_calls == 0);
    }
    {
        TestBackend backend;
        backend.recorder_open_ok = false;
        VoiceCapturePipeline pipeline(backend);
        VoiceCaptureError error = VoiceCaptureError::None;
        assert(!pipeline.open(error));
        assert(error == VoiceCaptureError::RecorderOpenFailed);
        assert(backend.release_calls == 1);
        assert(backend.recorder_close_calls == 0);
        pipeline.close();
        assert(backend.release_calls == 1);
    }
    {
        TestBackend backend;
        backend.resampler_open_ok = false;
        VoiceCapturePipeline pipeline(backend);
        VoiceCaptureError error = VoiceCaptureError::None;
        assert(!pipeline.open(error));
        assert(error == VoiceCaptureError::ResamplerFailed);
        assert(backend.release_calls == 1);
        assert(backend.recorder_close_calls == 1);
        assert(backend.resampler_close_calls == 0);
        pipeline.close();
        assert(backend.release_calls == 1);
    }
    for (const uint32_t capacity : {0u, 513u}) {
        TestBackend backend;
        backend.resampler_capacity = capacity;
        VoiceCapturePipeline pipeline(backend);
        VoiceCaptureError error = VoiceCaptureError::None;
        assert(!pipeline.open(error));
        assert(error == VoiceCaptureError::ResamplerFailed);
        assertClosedExactlyOnce(backend);
        pipeline.close();
        assertClosedExactlyOnce(backend);
    }
    {
        TestBackend backend;
        backend.resampler_capacity_ok = false;
        VoiceCapturePipeline pipeline(backend);
        VoiceCaptureError error = VoiceCaptureError::None;
        assert(!pipeline.open(error));
        assert(error == VoiceCaptureError::ResamplerFailed);
        assertClosedExactlyOnce(backend);
    }
}

void testCaptureFailuresMapAndCleanup()
{
    for (int failure = 0; failure < 3; ++failure) {
        TestBackend backend;
        backend.read_ok = failure != 0;
        backend.resample_ok = failure != 1;
        backend.encode_ok = failure != 2;
        VoiceCapturePipeline pipeline(backend);
        VoiceCaptureError error = VoiceCaptureError::None;
        assert(pipeline.open(error));
        std::array<uint8_t, VoiceCapturePipeline::kAdpcmBytesPerBlock> adpcm = {};
        uint32_t captured_samples = 99;
        assert(pipeline.captureBlock(adpcm, captured_samples, error) ==
               VoiceCaptureResult::Failed);
        assert(captured_samples == 0);
        assert(error == (failure == 0 ? VoiceCaptureError::RecorderOpenFailed
                                      : failure == 1
                                            ? VoiceCaptureError::ResamplerFailed
                                            : VoiceCaptureError::EncoderFailed));
        pipeline.close();
        assertClosedExactlyOnce(backend);
    }
}

void testStopAndCloseAreIdempotent()
{
    TestBackend backend;
    VoiceCapturePipeline pipeline(backend);
    VoiceCaptureError error = VoiceCaptureError::None;
    assert(pipeline.open(error));
    pipeline.requestStop();
    pipeline.requestStop();
    pipeline.close();
    pipeline.close();
    assertClosedExactlyOnce(backend);
}

void testStopDuringReadProcessesSuccessfulFinalBlock()
{
    TestBackend backend;
    backend.block_read = true;
    VoiceCapturePipeline pipeline(backend);
    VoiceCaptureError error = VoiceCaptureError::None;
    assert(pipeline.open(error));

    std::array<uint8_t, VoiceCapturePipeline::kAdpcmBytesPerBlock> adpcm = {};
    uint32_t captured_samples = 0;
    VoiceCaptureResult result = VoiceCaptureResult::Failed;
    std::thread capture([&] {
        result = pipeline.captureBlock(adpcm, captured_samples, error);
    });
    backend.waitUntilReadEntered();
    pipeline.requestStop();
    backend.unblockRead();
    capture.join();

    assert(result == VoiceCaptureResult::BlockReady);
    assert(captured_samples == VoiceCapturePipeline::kOutputSamplesPerBlock);
    assert(backend.resample_calls == 1);
    assert(backend.encode_calls == 1);
    assert(pipeline.captureBlock(adpcm, captured_samples, error) ==
           VoiceCaptureResult::Stopped);
    pipeline.close();
}

void testStoppedReadReturnsWithinBoundedTimeout()
{
    TestBackend backend;
    backend.block_read = true;
    backend.read_ok = false;
    VoiceCapturePipeline pipeline(backend);
    VoiceCaptureError error = VoiceCaptureError::None;
    assert(pipeline.open(error));

    std::array<uint8_t, VoiceCapturePipeline::kAdpcmBytesPerBlock> adpcm = {};
    uint32_t captured_samples = 0;
    VoiceCaptureResult result = VoiceCaptureResult::Failed;
    const auto started = std::chrono::steady_clock::now();
    std::thread capture([&] {
        result = pipeline.captureBlock(adpcm, captured_samples, error);
    });
    backend.waitUntilReadEntered();
    pipeline.requestStop();
    capture.join();
    const auto elapsed = std::chrono::steady_clock::now() - started;

    assert(result == VoiceCaptureResult::Stopped);
    assert(elapsed < std::chrono::milliseconds(300));
    assert(backend.last_read_timeout_ms ==
           VoiceCapturePipeline::kCaptureReadTimeoutMs);
    pipeline.close();
}

void testConcurrentPipelinesHaveOneInstanceOwnedLease()
{
    TestBackend first_backend;
    TestBackend second_backend;
    VoiceCapturePipeline first(first_backend);
    VoiceCapturePipeline second(second_backend);
    VoiceCaptureError first_error = VoiceCaptureError::None;
    VoiceCaptureError second_error = VoiceCaptureError::None;
    std::mutex start_mutex;
    std::condition_variable start_condition;
    int ready = 0;
    bool start = false;
    bool first_opened = false;
    bool second_opened = false;

    auto open = [&](VoiceCapturePipeline& pipeline, VoiceCaptureError& error,
                    bool& opened) {
        {
            std::unique_lock<std::mutex> lock(start_mutex);
            ++ready;
            start_condition.notify_all();
            start_condition.wait(lock, [&] { return start; });
        }
        opened = pipeline.open(error);
    };
    std::thread first_thread(open, std::ref(first), std::ref(first_error),
                             std::ref(first_opened));
    std::thread second_thread(open, std::ref(second), std::ref(second_error),
                              std::ref(second_opened));
    {
        std::unique_lock<std::mutex> lock(start_mutex);
        start_condition.wait(lock, [&] { return ready == 2; });
        start = true;
        start_condition.notify_all();
    }
    first_thread.join();
    second_thread.join();

    assert(first_opened != second_opened);
    VoiceCapturePipeline& winner = first_opened ? first : second;
    VoiceCapturePipeline& loser = first_opened ? second : first;
    TestBackend& winner_backend = first_opened ? first_backend : second_backend;
    VoiceCaptureError& loser_error = first_opened ? second_error : first_error;
    assert(loser_error == VoiceCaptureError::CaptureBusy);

    loser.close();
    assert(winner_backend.release_calls == 0);
    winner.close();
    assert(winner_backend.release_calls == 1);
}

void testBackendAcquireFailureImmediatelyReleasesGlobalLease()
{
    TestBackend failed_backend;
    failed_backend.acquire_ok = false;
    VoiceCapturePipeline failed(failed_backend);
    VoiceCaptureError error = VoiceCaptureError::None;
    assert(!failed.open(error));
    assert(error == VoiceCaptureError::CaptureBusy);

    TestBackend next_backend;
    VoiceCapturePipeline next(next_backend);
    error = VoiceCaptureError::None;
    assert(next.open(error));
    next.close();
}

}  // namespace

int main()
{
    testSuccessfulOpenCaptureClose();
    testPartialOpenFailuresRollbackExactlyOnce();
    testCaptureFailuresMapAndCleanup();
    testStopAndCloseAreIdempotent();
    testStopDuringReadProcessesSuccessfulFinalBlock();
    testStoppedReadReturnsWithinBoundedTimeout();
    testConcurrentPipelinesHaveOneInstanceOwnedLease();
    testBackendAcquireFailureImmediatelyReleasesGlobalLease();
    return 0;
}
