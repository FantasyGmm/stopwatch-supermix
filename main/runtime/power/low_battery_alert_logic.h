#pragma once

#include <cstdint>

enum class LowBatteryAlertPhase : uint8_t { Armed, LowPending, LowAlerted };

struct LowBatteryAlertInput {
    uint32_t now_ms = 0;
    bool battery_sample_due = false;
    bool battery_sample_valid = false;
    uint8_t battery_percent = 0;
    bool external_power = false;
    bool display_visible = true;
    bool playback_available = false;
    bool speaker_audible = true;
};

struct LowBatteryAlertActions {
    bool show_text = false;
    bool hide_text = false;
    bool play_sound = false;
    uint8_t battery_percent = 0;
};

class LowBatteryAlertLogic {
public:
    static constexpr uint8_t lowThreshold = 30;
    static constexpr uint8_t criticalThreshold = 10;
    static constexpr uint8_t rearmThreshold = 35;
    static constexpr uint8_t confirmationSamples = 2;
    static constexpr uint32_t textVisibleMs = 4000;

    LowBatteryAlertActions update(const LowBatteryAlertInput& input);
    void deferSound();
    LowBatteryAlertPhase phase() const { return _phase; }

private:
    LowBatteryAlertPhase _phase = LowBatteryAlertPhase::Armed;
    uint8_t _consecutive_low_samples = 0;
    uint8_t _consecutive_critical_samples = 0;
    uint8_t _alert_percent = 0;
    bool _episode_active = false;
    bool _critical_alerted = false;
    bool _sound_pending = false;
    bool _text_pending = false;
    bool _text_visible = false;
    uint32_t _text_visible_since_ms = 0;

    void rearm(LowBatteryAlertActions& actions);
    void armAlert(uint8_t battery_percent, bool critical);
    void refreshPhase();
};
