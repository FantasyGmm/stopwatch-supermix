#include "main/apps/app_codex_status/voice_input_session.h"

#include <cassert>
#include <cstdint>
#include <limits>

int main()
{
    using namespace voice_input;

    VoiceInputSession session;
    assert(session.state() == VoiceInputState::Unavailable);
    assert(!session.press(100));
    session.set_ready(true);
    assert(session.state() == VoiceInputState::Ready);
    assert(session.press(100));
    assert(session.state() == VoiceInputState::Starting);
    assert(!session.press(101));
    assert(!session.started(0, 110, 256));
    assert(session.started(7, 110, 256));
    assert(session.state() == VoiceInputState::Streaming);
    assert(!session.started(8, 111, 256));

    uint32_t sequence = 0;
    assert(session.allocate_audio_sequence(sequence));
    assert(sequence == 1);
    assert(session.allocate_audio_sequence(sequence));
    assert(sequence == 2);
    session.record_generated();
    session.record_accepted();
    session.record_queue_overflow();
    session.record_encode_failure();
    session.record_captured_samples(320);
    assert(session.release(900));
    assert(session.state() == VoiceInputState::Stopping);
    assert(!session.release(901));

    const EndSummary summary = session.summary(910);
    assert(summary.generated_packets == 1);
    assert(summary.accepted_packets == 1);
    assert(summary.queue_overflows == 1);
    assert(summary.encode_failures == 1);
    assert(summary.captured_samples == 320);
    assert(summary.elapsed_ms == 800);
    assert(summary.negotiated_mtu == 256);

    assert(session.stopped());
    assert(session.state() == VoiceInputState::Ready);
    assert(session.session() == 0);
    assert(!session.stopped());
    session.disconnect();
    assert(session.state() == VoiceInputState::Unavailable);

    session.set_ready(true);
    assert(session.press(1000));
    assert(session.started(8, 1010, 185));
    assert(session.cancel_for_home());
    assert(session.state() == VoiceInputState::Ready);
    assert(session.session() == 0);
    assert(!session.cancel_for_home());

    assert(session.press(1500));
    assert(session.started(10, 1510, 256));
    session.disconnect();
    assert(session.state() == VoiceInputState::Unavailable);
    assert(session.session() == 0);
    assert(!session.allocate_audio_sequence(sequence));

    session.set_ready(true);
    assert(session.press(2000));
    assert(session.release(2001));
    assert(session.state() == VoiceInputState::Stopping);
    assert(session.stopped());

    VoiceInputSession sequence_limit;
    sequence_limit.set_ready(true);
    assert(sequence_limit.press(0));
    assert(sequence_limit.started(9, 0, 256));
    sequence_limit.set_next_sequence_for_test(
        std::numeric_limits<uint32_t>::max());
    assert(!sequence_limit.allocate_audio_sequence(sequence));
    assert(sequence_limit.allocate_terminal_sequence(sequence));
    assert(sequence == std::numeric_limits<uint32_t>::max());
    assert(!sequence_limit.allocate_terminal_sequence(sequence));
    assert(sequence == std::numeric_limits<uint32_t>::max());
}
