#pragma once

#include "voice_input_protocol.h"

#include <cstdint>

namespace voice_input {

enum class VoiceInputState : uint8_t {
    Unavailable,
    Ready,
    Starting,
    Streaming,
    Stopping,
};

class VoiceInputSession {
public:
    VoiceInputState state() const;
    uint16_t session() const;

    void set_ready(bool ready);
    bool press(uint32_t now_ms);
    bool started(uint16_t session, uint32_t now_ms, uint16_t negotiated_mtu);
    bool release(uint32_t now_ms);
    bool stopped();
    bool cancel_for_home();
    void disconnect();

    bool allocate_audio_sequence(uint32_t& sequence);
    bool allocate_terminal_sequence(uint32_t& sequence);
    void record_generated();
    void record_accepted();
    void record_queue_overflow();
    void record_encode_failure();
    void record_captured_samples(uint32_t samples);
    EndSummary summary(uint32_t now_ms) const;

#if defined(STOPWATCH_HOST_TEST)
    void set_next_sequence_for_test(uint32_t sequence);
#endif

private:
    bool allocate_sequence(uint32_t& sequence, bool reserve_terminal);
    void reset_activity();
    bool active() const;

    VoiceInputState _state = VoiceInputState::Unavailable;
    uint16_t _session = 0;
    uint32_t _started_ms = 0;
    uint32_t _next_sequence = 1;
    bool _has_started = false;
    bool _sequence_exhausted = false;
    EndSummary _summary = {};
};

}  // namespace voice_input
