#include "ble_voice_probe_session.h"

#include <algorithm>

namespace ble_voice_probe {

namespace {

constexpr uint32_t kSyntheticNotificationsPerSecond = 50;

}  // namespace

bool Session::start(const ControlCommand& command, uint32_t now_ms, uint16_t negotiated_mtu)
{
    if (_active) {
        return false;
    }
    _command = command;
    _started_ms = now_ms;
    _next_sequence = 0;
    _summary = {};
    _crypto_elapsed_us.clear();
    _summary.negotiated_mtu = negotiated_mtu;
    _active = true;
    return true;
}

void Session::reset()
{
    _command = {};
    _started_ms = 0;
    _next_sequence = 0;
    _summary = {};
    _crypto_elapsed_us.clear();
    _active = false;
}

bool Session::active() const
{
    return _active;
}

uint16_t Session::id() const
{
    return _command.session;
}

Mode Session::mode() const
{
    return _command.mode;
}

uint32_t Session::target_bps() const
{
    return _command.target_bps;
}

uint32_t Session::allocate_sequence()
{
    return _next_sequence++;
}

uint32_t Session::synthetic_bytes_due(uint32_t now_ms) const
{
    if (!_active || _command.mode != Mode::Synthetic) {
        return 0;
    }
    const uint64_t elapsed = static_cast<uint32_t>(now_ms - _started_ms);
    const uint64_t expected = elapsed * _command.target_bps / 1000U;
    return expected > _summary.generated_payload_bytes
               ? static_cast<uint32_t>(expected - _summary.generated_payload_bytes)
               : 0;
}

uint16_t Session::synthetic_payload_size(uint32_t now_ms, uint16_t max_payload) const
{
    if (max_payload == 0) {
        return 0;
    }

    const uint32_t preferred_payload = std::min<uint32_t>(
        max_payload,
        (_command.target_bps + kSyntheticNotificationsPerSecond - 1U) /
            kSyntheticNotificationsPerSecond);
    const uint32_t due = synthetic_bytes_due(now_ms);
    if (due < preferred_payload) {
        return 0;
    }
    return static_cast<uint16_t>(std::min<uint32_t>(due, max_payload));
}

bool Session::duration_reached(uint32_t now_ms) const
{
    if (!_active) {
        return false;
    }
    const uint32_t elapsed = static_cast<uint32_t>(now_ms - _started_ms);
    return elapsed >= _command.duration_ms;
}

void Session::record_generated(uint16_t payload_bytes)
{
    if (!_active) {
        return;
    }
    ++_summary.generated_data_packets;
    _summary.generated_payload_bytes += payload_bytes;
}

void Session::record_notify_accepted()
{
    if (_active) {
        ++_summary.accepted_data_packets;
    }
}

void Session::record_queue_overflow()
{
    if (_active) {
        ++_summary.queue_overflows;
    }
}

void Session::record_encode_failure()
{
    if (_active) {
        ++_summary.encode_failures;
    }
}

void Session::record_captured_samples(uint32_t samples)
{
    if (_active) {
        _summary.captured_pcm_samples += samples;
    }
}

void Session::record_crypto_result(uint32_t elapsed_us, bool succeeded)
{
    if (!_active) {
        return;
    }
    ++_summary.crypto_packets;
    if (!succeeded) {
        ++_summary.crypto_failures;
    }
    _summary.crypto_total_us += elapsed_us;
    _summary.crypto_max_us = std::max(_summary.crypto_max_us, elapsed_us);
    _crypto_elapsed_us.push_back(elapsed_us);
}

StopSummary Session::summary(uint32_t now_ms) const
{
    StopSummary result = _summary;
    result.elapsed_ms = static_cast<uint32_t>(now_ms - _started_ms);
    if (!_crypto_elapsed_us.empty()) {
        std::vector<uint32_t> timings = _crypto_elapsed_us;
        const size_t rank = (timings.size() * 99U + 99U) / 100U;
        std::nth_element(
            timings.begin(), timings.begin() + static_cast<std::ptrdiff_t>(rank - 1U),
            timings.end());
        result.crypto_p99_us = timings[rank - 1U];
    }
    return result;
}

}  // namespace ble_voice_probe
