#include "ble_voice_probe_session.h"

namespace ble_voice_probe {

bool Session::start(const ControlCommand& command, uint32_t now_ms, uint16_t negotiated_mtu)
{
    if (_active) {
        return false;
    }
    _command = command;
    _started_ms = now_ms;
    _next_sequence = 0;
    _summary = {};
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

StopSummary Session::summary(uint32_t now_ms) const
{
    StopSummary result = _summary;
    result.elapsed_ms = static_cast<uint32_t>(now_ms - _started_ms);
    return result;
}

}  // namespace ble_voice_probe
