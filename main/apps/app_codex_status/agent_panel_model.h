#pragma once

#include "agent_registry.h"

#include <array>
#include <cstddef>

constexpr size_t kAgentPanelDisplayNameBytes = 32;

enum class AgentPanelRowState : uint8_t {
    Connected,
    Requesting,
    Offline,
};

enum class AgentPanelConfirmation : uint8_t {
    None,
    DeleteAgent,
    ResetAll,
};

enum class AgentPanelAction : uint8_t {
    None,
    Open,
    Close,
    AllowPairing,
    AllowAndSwitch,
    Reject,
    SwitchAgent,
    Delete,
    ConfirmDelete,
    ResetAll,
    ConfirmReset,
};

struct AgentPanelRow {
    secure_pairing::AgentFingerprint fingerprint = {};
    std::array<char, kAgentPanelDisplayNameBytes + 1> display_name = {};
    size_t display_name_size = 0;
    AgentPanelRowState state = AgentPanelRowState::Offline;
};

struct AgentPanelPairingRequest {
    bool present = false;
    secure_pairing::AgentFingerprint fingerprint = {};
    secure_pairing::AgentName display_name = {};
    size_t display_name_size = 0;
    std::array<char, 7> verification_code = {};
};

struct AgentPanelSnapshot {
    bool visible = false;
    bool switch_waiting_for_voice = false;
    AgentPanelConfirmation confirmation = AgentPanelConfirmation::None;
    secure_pairing::AgentFingerprint confirmation_fingerprint = {};
    std::array<AgentPanelRow, secure_pairing::kMaxAgents> rows = {};
    size_t count = 0;
    AgentPanelPairingRequest request = {};
};

struct AgentPanelDecision {
    AgentPanelAction action = AgentPanelAction::None;
    secure_pairing::AgentFingerprint fingerprint = {};
};

class AgentPanelModel {
public:
    void sync_registry(
        const secure_pairing::AgentRegistry& registry,
        const secure_pairing::AgentFingerprint* active,
        const secure_pairing::AgentFingerprint* pending);
    bool show_pairing_request(
        const secure_pairing::AgentName& display_name,
        size_t display_name_size,
        const secure_pairing::AgentFingerprint& fingerprint,
        const char* verification_code);
    AgentPanelDecision allow_pairing(bool switch_after);
    AgentPanelDecision reject_pairing();
    void clear_pairing_request();

    AgentPanelDecision request_switch(
        const secure_pairing::AgentFingerprint& fingerprint,
        bool voice_cleanup_required);
    AgentPanelDecision voice_cleanup_complete();
    AgentPanelDecision request_delete(
        const secure_pairing::AgentFingerprint& fingerprint);
    AgentPanelDecision request_reset();
    AgentPanelDecision confirm_destructive(bool accepted);

    void touch_press(int x, int y);
    void touch_move(int x, int y);
    AgentPanelAction touch_release();
    void cancel_touch();
    bool owns_touch() const;
    int vertical_drag_offset() const;
    void set_visible(bool visible);

    AgentPanelSnapshot snapshot() const;

private:
    bool contains(
        const secure_pairing::AgentFingerprint& fingerprint) const;
    AgentPanelDecision decision(
        AgentPanelAction action,
        const secure_pairing::AgentFingerprint& fingerprint = {}) const;

    AgentPanelSnapshot _snapshot = {};
    secure_pairing::AgentFingerprint _deferred_switch = {};
    bool _touch_active = false;
    bool _panel_drag_active = false;
    int _touch_start_x = 0;
    int _touch_start_y = 0;
    int _touch_x = 0;
    int _touch_y = 0;
};
