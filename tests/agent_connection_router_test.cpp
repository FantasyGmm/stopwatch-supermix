#include "main/apps/app_codex_status/agent_connection_router.h"

#include <cassert>
#include <cstdint>

int main()
{
    using namespace secure_pairing;

    AgentConnectionRouter router;
    assert(
        router.connected(10) ==
        AgentConnectionAdmission::Pending);
    assert(router.role(10) == AgentConnectionRole::Pending);
    assert(router.pending_handle() == 10);
    assert(router.active_handle() == kNoAgentConnection);
    assert(router.can_access(10, AgentGattSurface::Pairing));
    assert(!router.can_access(10, AgentGattSurface::Production));

    AgentFingerprint studio = {};
    studio[0] = 0x11;
    assert(router.authorize_pending(10, studio));
    assert(router.promote_pending(10));
    assert(router.role(10) == AgentConnectionRole::Active);
    assert(router.active_handle() == 10);
    assert(router.pending_handle() == kNoAgentConnection);
    assert(router.can_access(10, AgentGattSurface::Production));
    assert(!router.can_access(10, AgentGattSurface::Pairing));
    assert(router.active_fingerprint() == studio);

    assert(
        router.connected(11) ==
        AgentConnectionAdmission::Pending);
    assert(
        router.connected(12) ==
        AgentConnectionAdmission::RejectedFull);
    assert(router.role(12) == AgentConnectionRole::None);

    router.update_mtu(10, 256);
    router.update_mtu(11, 128);
    assert(router.update_voice_subscription(10, true));
    assert(!router.update_voice_subscription(11, true));
    assert(router.mtu(10) == 256);
    assert(router.mtu(11) == 128);
    assert(router.voice_subscribed(10));
    assert(!router.voice_subscribed(11));
    assert(router.active_mtu() == 256);
    assert(router.active_voice_subscribed());

    AgentFingerprint travel = {};
    travel[0] = 0x22;
    assert(router.authorize_pending(11, travel));
    assert(!router.promote_pending(11));
    const uint32_t active_generation = router.active_generation();
    assert(router.disconnected(10) == AgentConnectionRole::Active);
    assert(router.active_generation() == active_generation + 1);
    assert(router.active_handle() == kNoAgentConnection);
    assert(router.pending_handle() == 11);
    assert(router.role(11) == AgentConnectionRole::Pending);
    assert(!router.can_access(11, AgentGattSurface::Production));

    assert(router.promote_pending(11));
    assert(router.active_handle() == 11);
    assert(router.active_generation() == active_generation + 2);
    assert(router.active_mtu() == 128);
    assert(!router.active_voice_subscribed());
    assert(router.active_fingerprint() == travel);

    assert(router.disconnected(99) == AgentConnectionRole::None);
    assert(router.disconnected(11) == AgentConnectionRole::Active);
    assert(!router.has_connections());
    assert(router.active_mtu() == 23);
    assert(!router.active_voice_subscribed());

    assert(
        router.connected(20, UINT32_MAX - 10U) ==
        AgentConnectionAdmission::Pending);
    assert(
        router.expire_unauthorized_pending(5U, 20U) ==
        kNoAgentConnection);
    assert(router.pending_handle() == 20);
    assert(router.expire_unauthorized_pending(10U, 20U) == 20);
    assert(router.pending_handle() == kNoAgentConnection);

    assert(router.connected(21, 100U) == AgentConnectionAdmission::Pending);
    assert(router.authorize_pending(21, studio));
    assert(
        router.expire_unauthorized_pending(1000U, 20U) ==
        kNoAgentConnection);
    assert(router.pending_handle() == 21);
}
