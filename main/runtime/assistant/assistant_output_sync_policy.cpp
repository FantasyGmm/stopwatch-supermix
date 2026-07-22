#include "assistant_output_sync_policy.h"

#include <algorithm>

namespace {

uint64_t make_callback_state(uint32_t generation, uint32_t bits = 0)
{
    return (static_cast<uint64_t>(generation) << 32) | bits;
}

uint32_t callback_generation(uint64_t state)
{
    return static_cast<uint32_t>(state >> 32);
}

uint32_t callback_bits(uint64_t state)
{
    return static_cast<uint32_t>(state);
}

bool time_reached(uint32_t now_ms, uint32_t target_ms)
{
    return static_cast<int32_t>(now_ms - target_ms) >= 0;
}

} // namespace

void AssistantOutputSyncPolicy::reset()
{
    _pending_volume.store(-1);
    _callback_state.store(0);
    _active_generation = 0;
    _started_ms = 0;
    _next_retry_ms = 0;
    _in_flight_volume = -1;
    _in_flight = false;
}

void AssistantOutputSyncPolicy::request(int volume)
{
    _pending_volume.store(std::clamp(volume, 0, 100));
}

std::optional<AssistantOutputSyncRequest> AssistantOutputSyncPolicy::begin(
    uint32_t now_ms, bool runtime_running)
{
    if (!runtime_running || _in_flight || !time_reached(now_ms, _next_retry_ms)) {
        return std::nullopt;
    }

    const int volume = _pending_volume.load();
    if (volume < 0) {
        return std::nullopt;
    }

    _generation_counter++;
    if (_generation_counter == 0) {
        _generation_counter++;
    }
    _active_generation = _generation_counter;
    _in_flight_volume = volume;
    _started_ms = now_ms;
    _in_flight = true;
    _callback_state.store(make_callback_state(_active_generation));
    return AssistantOutputSyncRequest{volume, _active_generation};
}

void AssistantOutputSyncPolicy::markVolumeResult(uint32_t generation, bool success)
{
    markResult(generation, VolumeDone, VolumeFailed, success);
}

void AssistantOutputSyncPolicy::markMuteResult(uint32_t generation, bool success)
{
    markResult(generation, MuteDone, MuteFailed, success);
}

void AssistantOutputSyncPolicy::update(uint32_t now_ms)
{
    if (!_in_flight) {
        return;
    }

    const uint64_t callback_state = _callback_state.load();
    if (callback_generation(callback_state) != _active_generation) {
        return;
    }

    const uint32_t bits = callback_bits(callback_state);
    const bool completed = (bits & (VolumeDone | MuteDone)) ==
                           (VolumeDone | MuteDone);
    const bool timed_out = static_cast<uint32_t>(now_ms - _started_ms) >=
                           CompletionTimeoutMs;
    if (!completed && !timed_out) {
        return;
    }

    const bool succeeded = completed &&
                           (bits & (VolumeFailed | MuteFailed)) == 0;
    if (succeeded) {
        int expected = _in_flight_volume;
        _pending_volume.compare_exchange_strong(expected, -1);
    }

    _callback_state.store(0);
    _active_generation = 0;
    _in_flight_volume = -1;
    _in_flight = false;
    _next_retry_ms = succeeded ? now_ms : now_ms + RetryDelayMs;
}

int AssistantOutputSyncPolicy::pendingVolume() const
{
    return _pending_volume.load();
}

bool AssistantOutputSyncPolicy::inFlight() const
{
    return _in_flight;
}

void AssistantOutputSyncPolicy::markResult(uint32_t generation,
                                           uint32_t done_bit,
                                           uint32_t failed_bit,
                                           bool success)
{
    uint64_t current = _callback_state.load();
    while (callback_generation(current) == generation) {
        uint32_t bits = callback_bits(current) | done_bit;
        if (!success) {
            bits |= failed_bit;
        }
        const uint64_t desired = make_callback_state(generation, bits);
        if (_callback_state.compare_exchange_weak(current, desired)) {
            return;
        }
    }
}
