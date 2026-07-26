#pragma once

#include <cstdint>

enum class DisplayProtectionState : uint8_t {
    Active,
    Dimmed,
    ScreenOff,
};

enum class DisplayProtectionAction : uint8_t {
    None,
    Restore,
    Dim,
    ScreenOff,
};

enum class DisplayKeepAwakeReason : uint8_t {
    AssistantInteraction = 1U << 0,
    StopwatchRunning = 1U << 1,
    AlarmRinging = 1U << 2,
};

struct DisplayProtectionDecision {
    DisplayProtectionAction action = DisplayProtectionAction::None;
    bool consume_input = false;
};

class DisplayProtectionLogic {
public:
    static constexpr uint32_t DimAfterMs = 15000;
    static constexpr uint32_t ScreenOffAfterMs = 60000;

    void reset(uint32_t now_ms);
    DisplayProtectionDecision update(uint32_t now_ms);
    DisplayProtectionDecision userActivity(uint32_t now_ms);
    DisplayProtectionDecision setAutomaticEnabled(bool enabled, uint32_t now_ms);
    DisplayProtectionDecision setKeepAwake(DisplayKeepAwakeReason reason, bool active,
                                           uint32_t now_ms);
    DisplayProtectionState state() const;
    bool automaticEnabled() const;
    uint8_t reasonMask() const;
    static int dimBrightness(int user_brightness);

private:
    DisplayProtectionDecision restore(uint32_t now_ms, bool consume_input);
    bool heldAwake() const;

    DisplayProtectionState _state = DisplayProtectionState::Active;
    uint32_t _inactivity_origin_ms = 0;
    uint8_t _reason_mask = 0;
    bool _automatic_enabled = true;
};
