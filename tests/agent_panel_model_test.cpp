#include "main/apps/app_codex_status/agent_panel_model.h"

#include "main/apps/app_codex_status/secure_pairing_crypto.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>

namespace {

struct Entropy {
    uint8_t next = 1;
};

bool fill_entropy(void* context, uint8_t* output, size_t size)
{
    auto& entropy = *static_cast<Entropy*>(context);
    for (size_t index = 0; index < size; ++index) {
        output[index] = entropy.next++;
    }
    return true;
}

secure_pairing::AgentEnrollment enrollment(uint8_t scalar, const char* name)
{
    secure_pairing::crypto::PrivateScalar private_scalar = {};
    private_scalar.back() = scalar;
    secure_pairing::AgentEnrollment value = {};
    assert(secure_pairing::crypto::public_key_from_private(
        private_scalar, value.public_key));
    value.display_name_size = std::strlen(name);
    std::copy_n(
        name, value.display_name_size, value.display_name.begin());
    return value;
}

std::string row_name(const AgentPanelRow& row)
{
    return std::string(row.display_name.data(), row.display_name_size);
}

}  // namespace

int main()
{
    Entropy entropy = {};
    secure_pairing::AgentRegistry registry = {};
    assert(
        secure_pairing::AgentRegistry::initialize(
            fill_entropy, &entropy, registry) ==
        secure_pairing::RegistryResult::Ok);
    assert(
        registry.add(enrollment(1, "Studio")) ==
        secure_pairing::RegistryResult::Ok);
    assert(
        registry.add(enrollment(2, "Studio")) ==
        secure_pairing::RegistryResult::Ok);
    for (uint8_t scalar = 3; scalar <= 8; ++scalar) {
        const std::string name = "Agent " + std::to_string(scalar);
        assert(
            registry.add(enrollment(scalar, name.c_str())) ==
            secure_pairing::RegistryResult::Ok);
    }

    AgentPanelModel model;
    model.sync_registry(
        registry,
        &registry.records()[0].fingerprint,
        &registry.records()[1].fingerprint);
    AgentPanelSnapshot snapshot = model.snapshot();
    assert(snapshot.count == 8);
    assert(snapshot.rows[0].state == AgentPanelRowState::Connected);
    assert(snapshot.rows[1].state == AgentPanelRowState::Requesting);
    assert(snapshot.rows[2].state == AgentPanelRowState::Offline);
    assert(row_name(snapshot.rows[0]).find("Studio") == 0);
    assert(row_name(snapshot.rows[1]).find("Studio") == 0);
    assert(row_name(snapshot.rows[0]) != row_name(snapshot.rows[1]));
    assert(row_name(snapshot.rows[0]).size() <= kAgentPanelDisplayNameBytes);

    const auto pending = enrollment(9, "Travel Mac");
    secure_pairing::AgentFingerprint pending_fingerprint = {};
    pending_fingerprint[0] = 0x48;
    pending_fingerprint[1] = 0x27;
    assert(model.show_pairing_request(
        pending.display_name,
        pending.display_name_size,
        pending_fingerprint,
        "482731"));
    snapshot = model.snapshot();
    assert(snapshot.request.present);
    assert(std::string(snapshot.request.verification_code.data()) == "482731");
    assert(
        model.allow_pairing(false).action ==
        AgentPanelAction::AllowPairing);
    assert(
        model.allow_pairing(true).action ==
        AgentPanelAction::AllowAndSwitch);
    assert(model.reject_pairing().action == AgentPanelAction::Reject);
    assert(!model.snapshot().request.present);
    assert(!model.show_pairing_request(
        pending.display_name,
        pending.display_name_size,
        pending_fingerprint,
        "48A731"));

    const auto target = registry.records()[2].fingerprint;
    assert(
        model.request_switch(target, true).action ==
        AgentPanelAction::None);
    assert(model.snapshot().switch_waiting_for_voice);
    AgentPanelDecision deferred = model.voice_cleanup_complete();
    assert(deferred.action == AgentPanelAction::SwitchAgent);
    assert(deferred.fingerprint == target);
    assert(!model.snapshot().switch_waiting_for_voice);

    assert(
        model.request_delete(target).action ==
        AgentPanelAction::Delete);
    assert(
        model.snapshot().confirmation ==
        AgentPanelConfirmation::DeleteAgent);
    assert(
        model.confirm_destructive(false).action ==
        AgentPanelAction::None);
    assert(
        model.request_delete(target).action ==
        AgentPanelAction::Delete);
    AgentPanelDecision confirmed = model.confirm_destructive(true);
    assert(confirmed.action == AgentPanelAction::ConfirmDelete);
    assert(confirmed.fingerprint == target);

    assert(
        model.request_reset().action ==
        AgentPanelAction::ResetAll);
    assert(
        model.snapshot().confirmation ==
        AgentPanelConfirmation::ResetAll);
    assert(
        model.confirm_destructive(true).action ==
        AgentPanelAction::ConfirmReset);

    model.set_visible(false);
    model.touch_press(220, 30);
    assert(model.owns_touch());
    model.touch_move(220, 95);
    assert(
        model.touch_release() ==
        AgentPanelAction::Open);
    assert(model.snapshot().visible);

    model.touch_press(200, 300);
    model.touch_move(160, 302);
    assert(model.touch_release() == AgentPanelAction::None);
    assert(model.snapshot().visible);

    model.touch_press(220, 220);
    model.touch_move(220, 140);
    assert(model.vertical_drag_offset() == 0);
    assert(model.touch_release() == AgentPanelAction::None);
    assert(model.snapshot().visible);

    model.touch_press(220, 54);
    model.touch_move(220, 0);
    assert(
        model.touch_release() ==
        AgentPanelAction::Close);
    assert(!model.snapshot().visible);

    model.set_visible(false);
    model.touch_press(220, 30);
    model.touch_move(260, 35);
    assert(model.vertical_drag_offset() == 0);
    model.touch_move(220, 35);
    assert(model.vertical_drag_offset() == 0);
    model.touch_move(220, 56);
    assert(model.vertical_drag_offset() == 20);
    model.cancel_touch();

    model.set_visible(true);
    model.touch_press(220, 54);
    model.touch_move(220, 0);
    assert(model.vertical_drag_offset() == -48);
    model.cancel_touch();
}
