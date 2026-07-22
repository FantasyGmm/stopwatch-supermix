#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

struct AssistantOutputSyncRequest {
    int volume = 0;
    uint32_t generation = 0;
};

class AssistantOutputSyncPolicy {
public:
    static constexpr uint32_t CompletionTimeoutMs = 1500;
    static constexpr uint32_t RetryDelayMs = 250;

    void reset();
    void request(int volume);
    std::optional<AssistantOutputSyncRequest> begin(uint32_t now_ms, bool runtime_running);
    void markVolumeResult(uint32_t generation, bool success);
    void markMuteResult(uint32_t generation, bool success);
    void update(uint32_t now_ms);
    bool takeTimeoutNotice();

    int pendingVolume() const;
    bool inFlight() const;

private:
    enum CallbackBit : uint32_t {
        VolumeDone = 1U << 0,
        VolumeFailed = 1U << 1,
        MuteDone = 1U << 2,
        MuteFailed = 1U << 3,
    };

    std::atomic<int> _pending_volume{-1};
    std::atomic<uint64_t> _callback_state{0};
    uint32_t _generation_counter = 0;
    uint32_t _active_generation = 0;
    uint32_t _started_ms = 0;
    uint32_t _next_retry_ms = 0;
    int _in_flight_volume = -1;
    bool _in_flight = false;
    bool _timeout_reported = false;
    bool _timeout_notice_pending = false;

    void markResult(uint32_t generation, uint32_t done_bit,
                    uint32_t failed_bit, bool success);
};

struct AssistantOutputSyncSubmissionResult {
    bool volume_submitted = false;
    bool mute_submitted = false;
    bool volume_threw = false;
    bool mute_threw = false;

    bool allSubmitted() const
    {
        return volume_submitted && mute_submitted;
    }
};

template <typename VolumeSubmit, typename MuteSubmit>
AssistantOutputSyncSubmissionResult SubmitAssistantOutputSyncPair(
    AssistantOutputSyncPolicy& policy, uint32_t generation,
    VolumeSubmit&& submit_volume, MuteSubmit&& submit_mute) noexcept
{
    AssistantOutputSyncSubmissionResult result;
    try {
        result.volume_submitted = submit_volume();
    } catch (...) {
        result.volume_threw = true;
    }
    if (!result.volume_submitted && !result.volume_threw) {
        policy.markVolumeResult(generation, false);
    }

    try {
        result.mute_submitted = submit_mute();
    } catch (...) {
        result.mute_threw = true;
    }
    if (!result.mute_submitted && !result.mute_threw) {
        policy.markMuteResult(generation, false);
    }
    return result;
}
