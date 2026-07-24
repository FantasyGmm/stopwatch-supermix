#include "main/apps/app_codex_status/ble_voice_probe_session.h"

#include <cassert>
#include <cstdint>

int main()
{
    using namespace ble_voice_probe;

    Session session;
    ControlCommand command;
    command.command = Command::Start;
    command.mode = Mode::Synthetic;
    command.session = 7;
    command.target_bps = 8192;
    command.duration_ms = 60000;
    assert(session.start(command, 1000, 256));
    assert(session.allocate_sequence() == 0);
    assert(session.allocate_sequence() == 1);
    assert(session.synthetic_bytes_due(1000) == 0);
    assert(session.synthetic_payload_size(1019, 239) == 0);
    assert(session.synthetic_payload_size(1020, 239) == 0);
    assert(session.synthetic_payload_size(1021, 239) == 172);
    assert(session.synthetic_bytes_due(1100) == 819);
    session.record_generated(200);
    assert(session.synthetic_bytes_due(1100) == 619);
    session.record_notify_accepted();
    session.record_queue_overflow();
    session.record_encode_failure();
    session.record_captured_samples(320);
    assert(!session.duration_reached(60999));
    assert(session.duration_reached(61000));

    StopSummary summary = session.summary(61000);
    assert(summary.generated_data_packets == 1);
    assert(summary.accepted_data_packets == 1);
    assert(summary.queue_overflows == 1);
    assert(summary.encode_failures == 1);
    assert(summary.elapsed_ms == 60000);
    assert(summary.generated_payload_bytes == 200);
    assert(summary.captured_pcm_samples == 320);
    assert(summary.negotiated_mtu == 256);

    Session wrapped;
    assert(wrapped.start(command, UINT32_MAX - 20U, 185));
    assert(wrapped.synthetic_bytes_due(79) == 819);

    Session medium_rate;
    command.target_bps = 16384;
    assert(medium_rate.start(command, 0, 256));
    assert(medium_rate.synthetic_payload_size(14, 239) == 0);
    assert(medium_rate.synthetic_payload_size(15, 239) == 239);

    Session exploratory_rate;
    command.target_bps = 32768;
    assert(exploratory_rate.start(command, 0, 256));
    assert(exploratory_rate.synthetic_payload_size(7, 239) == 0);
    assert(exploratory_rate.synthetic_payload_size(8, 239) == 239);
}
