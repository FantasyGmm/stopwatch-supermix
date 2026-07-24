#include "main/apps/app_codex_status/ble_voice_probe_failure_transition.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace {

uint32_t read_u32(const uint8_t* bytes, size_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24U);
}

}  // namespace

int main()
{
    using namespace ble_voice_probe;

    ControlCommand command;
    command.command = Command::Start;
    command.mode = Mode::MicrophoneAdpcmAesCcm;
    command.session = 7;
    command.target_bps = 8192;
    command.duration_ms = 1000;

    Session session;
    assert(session.start(command, 0, 256));
    assert(session.allocate_sequence() == 0);

    Notification failed_data;
    failed_data.kind = Kind::Data;
    failed_data.session = command.session;
    failed_data.sequence = session.allocate_sequence();
    failed_data.watch_ms = 20;
    failed_data.payload_length = 172;
    session.record_crypto_result(37, false);

    detail::CryptoFailureTransition transition;
    assert(detail::prepare_crypto_failure_transition(
        session, failed_data, 20, transition));
    assert(transition.discard_pending_data);
    assert(transition.stop_requested);
    assert(!transition.producer_enabled);
    assert(transition.summary_prepared);
    assert(transition.summary.kind == Kind::StopSummary);
    assert(transition.summary.session == command.session);
    assert(transition.summary.sequence == failed_data.sequence);
    assert(transition.summary.payload_length == kEncryptedStopSummarySize);

    std::deque<Notification> notification_queue;
    notification_queue.push_back(transition.summary);
    const Notification& queued_summary = notification_queue.front();
    assert(queued_summary.kind == Kind::StopSummary);
    assert(read_u32(queued_summary.payload.data(), 30) == 1);
    assert(read_u32(queued_summary.payload.data(), 34) == 1);
    assert(read_u32(queued_summary.payload.data(), 38) == 37);
    assert(read_u32(queued_summary.payload.data(), 42) == 37);
    assert(read_u32(queued_summary.payload.data(), 46) == 37);

    Session no_failure;
    assert(no_failure.start(command, 0, 256));
    Notification no_failure_data = failed_data;
    no_failure_data.sequence = no_failure.allocate_sequence();
    detail::CryptoFailureTransition rejected;
    assert(!detail::prepare_crypto_failure_transition(
        no_failure, no_failure_data, 20, rejected));
}
