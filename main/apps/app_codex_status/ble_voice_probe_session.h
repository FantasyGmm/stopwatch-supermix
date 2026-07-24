#pragma once

#include "ble_voice_probe_protocol.h"

#include <cstdint>
#include <vector>

namespace ble_voice_probe {

class Session {
public:
    bool start(const ControlCommand& command, uint32_t now_ms, uint16_t negotiated_mtu);
    void reset();
    bool active() const;
    uint16_t id() const;
    Mode mode() const;
    uint32_t target_bps() const;
    uint32_t allocate_sequence();
    uint32_t synthetic_bytes_due(uint32_t now_ms) const;
    uint16_t synthetic_payload_size(uint32_t now_ms, uint16_t max_payload) const;
    bool duration_reached(uint32_t now_ms) const;
    void record_generated(uint16_t payload_bytes);
    void record_notify_accepted();
    void record_queue_overflow();
    void record_encode_failure();
    void record_captured_samples(uint32_t samples);
    void record_crypto_result(uint32_t elapsed_us, bool succeeded);
    StopSummary summary(uint32_t now_ms) const;

private:
    ControlCommand _command = {};
    uint32_t _started_ms = 0;
    uint32_t _next_sequence = 0;
    StopSummary _summary = {};
    std::vector<uint32_t> _crypto_elapsed_us;
    bool _active = false;
};

}  // namespace ble_voice_probe
