#include "agent_panel_model.h"

#include "agent_panel_ui_layout.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int kPanelPullStartMaxY = 70;
constexpr int kPanelPullDistance = 48;

size_t stored_name_size(const secure_pairing::AgentName& name)
{
    const auto end = std::find(name.begin(), name.end(), '\0');
    return static_cast<size_t>(end - name.begin());
}

bool valid_code(const char* code)
{
    if (code == nullptr || std::strlen(code) != 6) {
        return false;
    }
    return std::all_of(code, code + 6, [](char value) {
        return value >= '0' && value <= '9';
    });
}

bool nonzero(const secure_pairing::AgentFingerprint& fingerprint)
{
    return std::any_of(
        fingerprint.begin(), fingerprint.end(), [](uint8_t byte) {
            return byte != 0;
        });
}

}  // namespace

void AgentPanelModel::sync_registry(
    const secure_pairing::AgentRegistry& registry,
    const secure_pairing::AgentFingerprint* active,
    const secure_pairing::AgentFingerprint* pending)
{
    _snapshot.rows = {};
    _snapshot.count =
        std::min(registry.count(), secure_pairing::kMaxAgents);
    for (size_t index = 0; index < _snapshot.count; ++index) {
        const secure_pairing::AgentRecord& record =
            registry.records()[index];
        AgentPanelRow& row = _snapshot.rows[index];
        row.fingerprint = record.fingerprint;
        const size_t raw_size = stored_name_size(record.display_name);
        const size_t copy_size =
            std::min(raw_size, kAgentPanelDisplayNameBytes);
        std::copy_n(
            record.display_name.begin(),
            copy_size,
            row.display_name.begin());
        row.display_name_size = copy_size;
        row.display_name[row.display_name_size] = '\0';
        std::snprintf(
            row.short_id.data(),
            row.short_id.size(),
            "%02X%02X",
            record.fingerprint[0],
            record.fingerprint[1]);
        row.last_connected_at = record.last_connected_at;
        if (active != nullptr && record.fingerprint == *active) {
            row.state = AgentPanelRowState::Connected;
        } else if (
            pending != nullptr && record.fingerprint == *pending) {
            row.state = AgentPanelRowState::Requesting;
        } else {
            row.state = AgentPanelRowState::Offline;
        }
    }
    if (_snapshot.confirmation == AgentPanelConfirmation::DeleteAgent &&
        !contains(_snapshot.confirmation_fingerprint)) {
        _snapshot.confirmation = AgentPanelConfirmation::None;
        _snapshot.confirmation_fingerprint = {};
    }
}

bool AgentPanelModel::show_pairing_request(
    const secure_pairing::AgentName& display_name,
    size_t display_name_size,
    const secure_pairing::AgentFingerprint& fingerprint,
    const char* verification_code)
{
    if (display_name_size == 0 ||
        display_name_size > secure_pairing::kAgentNameBytes ||
        !nonzero(fingerprint) || !valid_code(verification_code)) {
        return false;
    }
    _snapshot.request = {};
    _snapshot.request.present = true;
    _snapshot.request.fingerprint = fingerprint;
    _snapshot.request.display_name_size = display_name_size;
    std::copy_n(
        display_name.begin(),
        display_name_size,
        _snapshot.request.display_name.begin());
    _snapshot.request.display_name[display_name_size] = '\0';
    std::copy_n(
        verification_code,
        6,
        _snapshot.request.verification_code.begin());
    _snapshot.request.verification_code[6] = '\0';
    _snapshot.visible = true;
    return true;
}

AgentPanelDecision AgentPanelModel::allow_pairing(bool switch_after)
{
    if (!_snapshot.request.present) {
        return {};
    }
    return decision(
        switch_after
            ? AgentPanelAction::AllowAndSwitch
            : AgentPanelAction::AllowPairing,
        _snapshot.request.fingerprint);
}

AgentPanelDecision AgentPanelModel::reject_pairing()
{
    if (!_snapshot.request.present) {
        return {};
    }
    const auto fingerprint = _snapshot.request.fingerprint;
    clear_pairing_request();
    return decision(AgentPanelAction::Reject, fingerprint);
}

void AgentPanelModel::clear_pairing_request()
{
    _snapshot.request = {};
}

AgentPanelDecision AgentPanelModel::request_switch(
    const secure_pairing::AgentFingerprint& fingerprint,
    bool voice_cleanup_required)
{
    if (!contains(fingerprint)) {
        return {};
    }
    if (voice_cleanup_required) {
        _deferred_switch = fingerprint;
        _snapshot.switch_waiting_for_voice = true;
        return {};
    }
    return decision(AgentPanelAction::SwitchAgent, fingerprint);
}

AgentPanelDecision AgentPanelModel::voice_cleanup_complete()
{
    if (!_snapshot.switch_waiting_for_voice) {
        return {};
    }
    const auto fingerprint = _deferred_switch;
    _deferred_switch = {};
    _snapshot.switch_waiting_for_voice = false;
    return decision(AgentPanelAction::SwitchAgent, fingerprint);
}

AgentPanelDecision AgentPanelModel::request_delete(
    const secure_pairing::AgentFingerprint& fingerprint)
{
    if (!contains(fingerprint)) {
        return {};
    }
    _snapshot.confirmation = AgentPanelConfirmation::DeleteAgent;
    _snapshot.confirmation_fingerprint = fingerprint;
    return decision(AgentPanelAction::Delete, fingerprint);
}

AgentPanelDecision AgentPanelModel::request_reset()
{
    _snapshot.confirmation = AgentPanelConfirmation::ResetAll;
    _snapshot.confirmation_fingerprint = {};
    return decision(AgentPanelAction::ResetAll);
}

AgentPanelDecision AgentPanelModel::confirm_destructive(bool accepted)
{
    const AgentPanelConfirmation confirmation = _snapshot.confirmation;
    const auto fingerprint = _snapshot.confirmation_fingerprint;
    _snapshot.confirmation = AgentPanelConfirmation::None;
    _snapshot.confirmation_fingerprint = {};
    if (!accepted) {
        return {};
    }
    if (confirmation == AgentPanelConfirmation::DeleteAgent) {
        return decision(AgentPanelAction::ConfirmDelete, fingerprint);
    }
    if (confirmation == AgentPanelConfirmation::ResetAll) {
        return decision(AgentPanelAction::ConfirmReset);
    }
    return {};
}

void AgentPanelModel::touch_press(int x, int y)
{
    const bool can_start = _snapshot.visible || y <= kPanelPullStartMaxY;
    const bool in_panel_drag_region =
        y < agent_panel_ui::kPanelHeaderBottomY ||
        y >= agent_panel_ui::kCloseHint.y;
    _touch_active = can_start;
    _panel_drag_active =
        can_start &&
        (!_snapshot.visible || in_panel_drag_region);
    _touch_start_x = x;
    _touch_start_y = y;
    _touch_x = x;
    _touch_y = y;
}

void AgentPanelModel::touch_move(int x, int y)
{
    if (!_touch_active) {
        return;
    }
    _touch_x = x;
    _touch_y = y;
}

AgentPanelAction AgentPanelModel::touch_release()
{
    if (!_touch_active) {
        return AgentPanelAction::None;
    }
    _touch_active = false;
    const int dx = _touch_x - _touch_start_x;
    const int dy = _touch_y - _touch_start_y;
    if (std::abs(dy) <= std::abs(dx)) {
        return AgentPanelAction::None;
    }
    if (!_snapshot.visible && dy >= kPanelPullDistance) {
        _snapshot.visible = true;
        return AgentPanelAction::Open;
    }
    if (_snapshot.visible && _panel_drag_active &&
        dy <= -kPanelPullDistance) {
        _snapshot.visible = false;
        return AgentPanelAction::Close;
    }
    return AgentPanelAction::None;
}

void AgentPanelModel::cancel_touch()
{
    _touch_active = false;
    _panel_drag_active = false;
}

bool AgentPanelModel::owns_touch() const
{
    return _touch_active || _snapshot.visible;
}

int AgentPanelModel::vertical_drag_offset() const
{
    constexpr int kDragDeadZone = 6;
    if (!_touch_active || !_panel_drag_active) {
        return 0;
    }
    const int dx = _touch_x - _touch_start_x;
    const int dy = _touch_y - _touch_start_y;
    if (std::abs(dy) <= std::abs(dx) ||
        std::abs(dy) <= kDragDeadZone) {
        return 0;
    }
    return dy > 0 ? dy - kDragDeadZone : dy + kDragDeadZone;
}

void AgentPanelModel::set_visible(bool visible)
{
    _snapshot.visible = visible;
    _touch_active = false;
    _panel_drag_active = false;
}

AgentPanelSnapshot AgentPanelModel::snapshot() const
{
    return _snapshot;
}

bool AgentPanelModel::contains(
    const secure_pairing::AgentFingerprint& fingerprint) const
{
    return std::any_of(
        _snapshot.rows.begin(),
        _snapshot.rows.begin() +
            static_cast<std::ptrdiff_t>(_snapshot.count),
        [&fingerprint](const AgentPanelRow& row) {
            return row.fingerprint == fingerprint;
        });
}

AgentPanelDecision AgentPanelModel::decision(
    AgentPanelAction action,
    const secure_pairing::AgentFingerprint& fingerprint) const
{
    return AgentPanelDecision{action, fingerprint};
}
