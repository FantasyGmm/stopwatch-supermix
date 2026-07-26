#include "voice_input_session.h"

#include <limits>

namespace voice_input {

VoiceInputState VoiceInputSession::state() const
{
    return _state;
}

uint16_t VoiceInputSession::session() const
{
    return _session;
}

void VoiceInputSession::set_ready(bool ready)
{
    if (!ready) {
        disconnect();
    } else if (_state == VoiceInputState::Unavailable) {
        _state = VoiceInputState::Ready;
    }
}

bool VoiceInputSession::press(uint32_t now_ms)
{
    (void)now_ms;
    if (_state != VoiceInputState::Ready) {
        return false;
    }
    reset_activity();
    _state = VoiceInputState::Starting;
    return true;
}

bool VoiceInputSession::started(
    uint16_t session,
    uint32_t now_ms,
    uint16_t negotiated_mtu)
{
    if (_state != VoiceInputState::Starting || session == 0) {
        return false;
    }
    _session = session;
    _started_ms = now_ms;
    _has_started = true;
    _summary.negotiated_mtu = negotiated_mtu;
    _state = VoiceInputState::Streaming;
    return true;
}

bool VoiceInputSession::release(uint32_t now_ms)
{
    (void)now_ms;
    if (_state != VoiceInputState::Starting &&
        _state != VoiceInputState::Streaming) {
        return false;
    }
    _state = VoiceInputState::Stopping;
    return true;
}

bool VoiceInputSession::stopped()
{
    if (_state != VoiceInputState::Stopping) {
        return false;
    }
    reset_activity();
    _state = VoiceInputState::Ready;
    return true;
}

bool VoiceInputSession::cancel_for_home()
{
    if (!active()) {
        return false;
    }
    reset_activity();
    _state = VoiceInputState::Ready;
    return true;
}

void VoiceInputSession::disconnect()
{
    reset_activity();
    _state = VoiceInputState::Unavailable;
}

bool VoiceInputSession::allocate_audio_sequence(uint32_t& sequence)
{
    return allocate_sequence(sequence, true);
}

bool VoiceInputSession::allocate_terminal_sequence(uint32_t& sequence)
{
    return allocate_sequence(sequence, false);
}

bool VoiceInputSession::allocate_sequence(
    uint32_t& sequence,
    bool reserve_terminal)
{
    if ((_state != VoiceInputState::Streaming &&
         _state != VoiceInputState::Stopping) ||
        !_has_started || _sequence_exhausted ||
        (reserve_terminal &&
         _next_sequence == std::numeric_limits<uint32_t>::max())) {
        return false;
    }
    sequence = _next_sequence;
    if (_next_sequence == std::numeric_limits<uint32_t>::max()) {
        _sequence_exhausted = true;
    } else {
        ++_next_sequence;
    }
    return true;
}

void VoiceInputSession::record_generated()
{
    if (active()) {
        ++_summary.generated_packets;
    }
}

void VoiceInputSession::record_accepted()
{
    if (active()) {
        ++_summary.accepted_packets;
    }
}

void VoiceInputSession::record_queue_overflow()
{
    if (active()) {
        ++_summary.queue_overflows;
    }
}

void VoiceInputSession::record_encode_failure()
{
    if (active()) {
        ++_summary.encode_failures;
    }
}

void VoiceInputSession::record_captured_samples(uint32_t samples)
{
    if (active()) {
        _summary.captured_samples += samples;
    }
}

EndSummary VoiceInputSession::summary(uint32_t now_ms) const
{
    EndSummary result = _summary;
    result.elapsed_ms = _has_started
                            ? static_cast<uint32_t>(now_ms - _started_ms)
                            : 0;
    return result;
}

#if defined(STOPWATCH_HOST_TEST)
void VoiceInputSession::set_next_sequence_for_test(uint32_t sequence)
{
    _next_sequence = sequence;
    _sequence_exhausted = false;
}
#endif

void VoiceInputSession::reset_activity()
{
    _session = 0;
    _started_ms = 0;
    _next_sequence = 1;
    _has_started = false;
    _sequence_exhausted = false;
    _summary = {};
}

bool VoiceInputSession::active() const
{
    return _state == VoiceInputState::Starting ||
           _state == VoiceInputState::Streaming ||
           _state == VoiceInputState::Stopping;
}

}  // namespace voice_input
