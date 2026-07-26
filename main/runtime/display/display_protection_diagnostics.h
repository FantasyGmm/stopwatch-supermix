#pragma once

#include "display_protection_config.h"
#include "display_protection_logic.h"

#include <hal/external_power_state.h>

#include <cstddef>
#include <cstdint>

enum class DisplayWakeReason : uint8_t {
    Initialization,
    Button,
    Touch,
    PowerInserted,
    PowerRemoved,
    ConfigurationChanged,
    KeepAwakeActivated,
};

struct DisplayProtectionDiagnostics {
    DisplayProtectionState state = DisplayProtectionState::Active;
    int effective_brightness = 0;
    int user_brightness = 80;
    bool automatic_enabled = true;
    ExternalPowerSnapshot external_power{};
    DisplayProtectionConfig config = defaultDisplayProtectionConfig();
    DisplayProtectionTimingSnapshot timing{};
    uint8_t reason_mask = 0;
    DisplayWakeReason last_wake_reason = DisplayWakeReason::Initialization;
};

const char* DisplayProtectionStateName(DisplayProtectionState state);
const char* ExternalPowerStateName(ExternalPowerState state);
const char* DisplayWakeReasonName(DisplayWakeReason reason);
size_t FormatDisplayProtectionDiagnostics(
    const DisplayProtectionDiagnostics& diagnostics, char* output,
    size_t output_size);

enum class ExternalPowerHealthLogEvent : uint8_t {
    None,
    Unhealthy,
    Healthy,
};

class ExternalPowerHealthLogPolicy {
public:
    static constexpr uint32_t RetryMs = 30000U;

    void reset(bool sample_valid, uint32_t now_ms);
    ExternalPowerHealthLogEvent update(bool sample_valid, uint32_t now_ms);

private:
    uint32_t _last_log_ms = 0;
    bool _last_sample_valid = false;
    bool _initialized = false;
};
