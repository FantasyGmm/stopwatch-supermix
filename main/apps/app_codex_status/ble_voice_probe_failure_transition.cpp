#include "ble_voice_probe_failure_transition.h"

namespace ble_voice_probe::detail {

bool prepare_crypto_failure_transition(
    const Session& session,
    const Notification& failed_data,
    uint32_t now_ms,
    CryptoFailureTransition& transition
)
{
    const Mode mode = session.mode();
    if (!session.active() ||
        (mode != Mode::MicrophoneAdpcmAesCcm &&
         mode != Mode::SyntheticAesCcm) ||
        failed_data.kind != Kind::Data ||
        failed_data.session != session.id()) {
        return false;
    }

    const StopSummary counters = session.summary(now_ms);
    if (counters.crypto_packets == 0 || counters.crypto_failures == 0) {
        return false;
    }

    CryptoFailureTransition candidate;
    candidate.summary.kind = Kind::StopSummary;
    candidate.summary.session = session.id();
    candidate.summary.sequence = failed_data.sequence;
    candidate.summary.watch_ms = now_ms;
    candidate.summary.payload_length = encode_stop_summary(
        counters,
        mode,
        candidate.summary.payload.data(),
        candidate.summary.payload.size());
    if (candidate.summary.payload_length == 0) {
        return false;
    }

    candidate.discard_pending_data = true;
    candidate.stop_requested = true;
    candidate.producer_enabled = false;
    candidate.summary_prepared = true;
    transition = candidate;
    return true;
}

}  // namespace ble_voice_probe::detail
