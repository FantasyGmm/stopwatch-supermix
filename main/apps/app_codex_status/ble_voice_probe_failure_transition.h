#pragma once

#include "ble_voice_probe_protocol.h"
#include "ble_voice_probe_session.h"

#include <cstdint>

namespace ble_voice_probe::detail {

struct CryptoFailureTransition {
    Notification summary = {};
    bool discard_pending_data = false;
    bool stop_requested = false;
    bool producer_enabled = true;
    bool summary_prepared = false;
};

bool prepare_crypto_failure_transition(
    const Session& session,
    const Notification& failed_data,
    uint32_t now_ms,
    CryptoFailureTransition& transition
);

}  // namespace ble_voice_probe::detail
