#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace ble_voice_probe::detail {

class MicrophonePcmBuffer {
public:
    static constexpr size_t kBlockSamples = 320;
    using Block = std::array<int16_t, kBlockSamples>;

    void append(const int16_t* samples, size_t count)
    {
        if (samples == nullptr || count == 0) {
            return;
        }
        _retained.insert(_retained.end(), samples, samples + count);
        while (_retained.size() >= kBlockSamples) {
            Block block = {};
            std::copy_n(_retained.begin(), kBlockSamples, block.begin());
            _complete.push_back(block);
            _retained.erase(_retained.begin(), _retained.begin() + kBlockSamples);
        }
    }

    bool has_complete_block() const
    {
        return !_complete.empty();
    }

    bool take_complete_block(Block& block)
    {
        if (_complete.empty()) {
            return false;
        }
        block = _complete.front();
        _complete.pop_front();
        return true;
    }

    size_t complete_blocks() const
    {
        return _complete.size();
    }

    size_t retained_samples() const
    {
        return _retained.size();
    }

    void clear()
    {
        _complete.clear();
        _retained.clear();
    }

private:
    std::deque<Block> _complete;
    std::vector<int16_t> _retained;
};

}  // namespace ble_voice_probe::detail

enum class VoiceCaptureError : uint8_t {
    None = 0,
    CaptureBusy,
    RecorderOpenFailed,
    ResamplerFailed,
    EncoderFailed,
};

enum class VoiceCaptureResult : uint8_t {
    Continue = 0,
    BlockReady,
    Stopped,
    Failed,
};

class VoiceCapturePipeline {
public:
    static constexpr uint32_t kCaptureReadTimeoutMs = 100;
    static constexpr size_t kOutputSamplesPerBlock = 320;
    static constexpr size_t kAdpcmBytesPerBlock = 164;

    VoiceCapturePipeline() = default;
    ~VoiceCapturePipeline();

    VoiceCapturePipeline(const VoiceCapturePipeline&) = delete;
    VoiceCapturePipeline& operator=(const VoiceCapturePipeline&) = delete;

    bool open(VoiceCaptureError& error);
    VoiceCaptureResult captureBlock(
        std::array<uint8_t, kAdpcmBytesPerBlock>& adpcm,
        uint32_t& captured_samples,
        VoiceCaptureError& error);
    void requestStop();
    void close();

private:
    static constexpr size_t kCaptureInputSamples = 882;

    VoiceCaptureResult encodePendingBlock(
        std::array<uint8_t, kAdpcmBytesPerBlock>& adpcm,
        uint32_t& captured_samples,
        VoiceCaptureError& error);

    void* _resampler = nullptr;
    std::atomic<bool> _stop_requested{true};
    std::array<int16_t, kCaptureInputSamples> _capture_input = {};
    std::vector<int16_t> _resampler_output;
    ble_voice_probe::detail::MicrophonePcmBuffer _microphone_pcm;
    uint32_t _resampler_output_capacity = 0;
    bool _capture_owned = false;
    bool _recorder_open = false;
    bool _open = false;
};
