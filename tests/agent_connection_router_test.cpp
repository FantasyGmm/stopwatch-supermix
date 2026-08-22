#include "main/apps/app_codex_status/agent_connection_router.h"
#include "main/apps/app_codex_status/codex_pet_transfer.h"
#include "main/apps/app_codex_status/connection_termination_guard.h"

#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace {

void test_active_voice_link_is_one_coherent_snapshot()
{
    using namespace secure_pairing;

    AgentConnectionRouter router;
    AgentFingerprint fingerprint = {};
    fingerprint[0] = 0x44;

    assert(router.connected(31, 100) == AgentConnectionAdmission::Pending);
    assert(router.authorize_pending(31, fingerprint));
    assert(router.promote_pending(31));
    assert(router.update_mtu(31, 256));
    assert(router.update_voice_subscription(31, true));

    const AgentVoiceLink active = router.active_voice_link();
    assert(active.handle == 31);
    assert(active.generation != 0);
    assert(active.mtu == 256);
    assert(active.subscribed);
    assert(active.authorized);

    assert(router.disconnected(31) == AgentConnectionRole::Active);
    const AgentVoiceLink disconnected = router.active_voice_link();
    assert(disconnected.handle == kNoAgentConnection);
    assert(disconnected.generation != active.generation);
    assert(!disconnected.subscribed);
    assert(!disconnected.authorized);
}

void test_production_surface_checks_do_not_mutate_voice_subscription()
{
    using namespace secure_pairing;

    AgentConnectionRouter router;
    AgentFingerprint fingerprint = {};
    fingerprint[0] = 0x51;
    assert(router.connected(41) == AgentConnectionAdmission::Pending);
    assert(router.authorize_pending(41, fingerprint));
    assert(router.promote_pending(41));
    assert(router.update_mtu(41, 256));
    assert(router.update_voice_subscription(41, true));
    assert(router.connected(42) == AgentConnectionAdmission::Pending);

    const AgentVoiceLink before = router.active_voice_link();
    assert(router.can_access(41, AgentGattSurface::Production));
    assert(!router.can_access(42, AgentGattSurface::Production));
    const AgentVoiceLink after = router.active_voice_link();
    assert(after.handle == before.handle);
    assert(after.generation == before.generation);
    assert(after.mtu == before.mtu);
    assert(after.subscribed == before.subscribed);
    assert(after.authorized == before.authorized);
}

void test_pet_subscription_generation_exposes_fast_loss_and_resubscribe()
{
    codex_pet::PetSubscriptionTracker tracker;
    const auto enabled = tracker.update(true);
    assert(enabled.changed);
    assert(!enabled.lost);
    assert(enabled.snapshot.subscribed);
    assert(enabled.snapshot.generation == 1);

    const auto lost = tracker.update(false);
    assert(lost.changed);
    assert(lost.lost);
    assert(!lost.snapshot.subscribed);
    assert(lost.snapshot.generation == 2);

    const auto resubscribed = tracker.update(true);
    assert(resubscribed.changed);
    assert(!resubscribed.lost);
    assert(resubscribed.snapshot.subscribed);
    assert(resubscribed.snapshot.generation == 3);
    assert(!tracker.accepts(enabled.snapshot.generation));
    assert(tracker.accepts(resubscribed.snapshot.generation));
}

void test_pet_subscription_generation_saturation_fails_closed_consistently()
{
    constexpr uint64_t kSaturatedGeneration = 0x7fffffffU;

    const auto entered_saturation =
        codex_pet::PetSubscriptionTracker::advance(
            {kSaturatedGeneration - 1U, false}, true);
    assert(entered_saturation.changed);
    assert(!entered_saturation.lost);
    assert(!entered_saturation.snapshot.subscribed);
    assert(
        entered_saturation.snapshot.generation == kSaturatedGeneration);

    const auto saturated = codex_pet::PetSubscriptionTracker::advance(
        entered_saturation.snapshot, true);
    assert(!saturated.changed);
    assert(!saturated.lost);
    assert(!saturated.snapshot.subscribed);
    assert(saturated.snapshot.generation == kSaturatedGeneration);

    const auto lost_at_limit = codex_pet::PetSubscriptionTracker::advance(
        {kSaturatedGeneration, true}, false);
    assert(lost_at_limit.changed);
    assert(lost_at_limit.lost);
    assert(!lost_at_limit.snapshot.subscribed);
    assert(lost_at_limit.snapshot.generation == kSaturatedGeneration);
}

void test_connection_termination_holds_router_guard_through_submission()
{
    std::mutex router_guard;
    std::mutex event_mutex;
    std::condition_variable event_changed;
    bool submission_entered = false;
    bool allow_submission_to_finish = false;
    int result = 0;

    std::thread submitter([&]() {
        result = secure_pairing::serialize_connection_termination(
            router_guard,
            [&]() {
                std::unique_lock<std::mutex> lock(event_mutex);
                submission_entered = true;
                event_changed.notify_one();
                event_changed.wait(lock, [&]() {
                    return allow_submission_to_finish;
                });
                return 17;
            });
    });

    {
        std::unique_lock<std::mutex> lock(event_mutex);
        event_changed.wait(lock, [&]() { return submission_entered; });
    }
    const bool guard_was_available = router_guard.try_lock();
    if (guard_was_available) {
        router_guard.unlock();
    }
    assert(!guard_was_available);

    {
        std::lock_guard<std::mutex> lock(event_mutex);
        allow_submission_to_finish = true;
    }
    event_changed.notify_one();
    submitter.join();
    assert(result == 17);
}

}  // namespace

int main()
{
    using namespace secure_pairing;

    test_active_voice_link_is_one_coherent_snapshot();
    test_production_surface_checks_do_not_mutate_voice_subscription();
    test_pet_subscription_generation_exposes_fast_loss_and_resubscribe();
    test_pet_subscription_generation_saturation_fails_closed_consistently();
    test_connection_termination_holds_router_guard_through_submission();

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
    assert(router.update_pairing_subscription(10, true));
    assert(router.promote_pending(10));
    const auto first_active = router.active_connection();
    assert(first_active.handle == 10);
    assert(first_active.generation == router.active_generation());
    assert(router.role(10) == AgentConnectionRole::Active);
    assert(router.active_handle() == 10);
    assert(router.pending_handle() == kNoAgentConnection);
    assert(router.can_access(10, AgentGattSurface::Production));
    assert(router.can_access(10, AgentGattSurface::Pairing));
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
    assert(router.pairing_subscribed(10));

    AgentFingerprint travel = {};
    travel[0] = 0x22;
    assert(router.authorize_pending(11, travel));
    assert(!router.promote_pending(11));
    const uint32_t active_generation = router.active_generation();
    assert(router.disconnected(10) == AgentConnectionRole::Active);
    const auto disconnected = router.active_connection();
    assert(disconnected.handle == kNoAgentConnection);
    assert(disconnected.generation != first_active.generation);
    assert(router.active_generation() == active_generation + 1);
    assert(router.active_handle() == kNoAgentConnection);
    assert(router.pending_handle() == 11);
    assert(router.role(11) == AgentConnectionRole::Pending);
    assert(!router.can_access(11, AgentGattSurface::Production));

    assert(router.promote_pending(11));
    const auto second_active = router.active_connection();
    assert(second_active.handle == 11);
    assert(second_active.generation != first_active.generation);
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
        router.expire_unauthorized_pending(5U, 20U, 40U) ==
        kNoAgentConnection);
    assert(router.pending_handle() == 20);
    assert(router.expire_unauthorized_pending(10U, 20U, 40U) == 20);
    assert(router.pending_handle() == kNoAgentConnection);
    assert(router.disconnecting_handle() == 20);
    assert(router.role(20) == AgentConnectionRole::Disconnecting);
    assert(!router.can_access(20, AgentGattSurface::Pairing));
    assert(
        router.connected(21, 100U) ==
        AgentConnectionAdmission::RejectedFull);
    assert(router.disconnected(20) == AgentConnectionRole::Disconnecting);

    assert(router.connected(21, 100U) == AgentConnectionAdmission::Pending);
    assert(router.authorize_pending(21, studio));
    assert(
        router.expire_unauthorized_pending(119U, 20U, 40U) ==
        kNoAgentConnection);
    assert(router.pending_handle() == 21);
    assert(router.expire_unauthorized_pending(140U, 20U, 40U) == 21);
    assert(router.disconnecting_handle() == 21);
    assert(router.disconnected(21) == AgentConnectionRole::Disconnecting);

    assert(router.connected(22, 100U) == AgentConnectionAdmission::Pending);
    assert(router.note_pending_activity(22, 115U));
    assert(
        router.expire_unauthorized_pending(125U, 20U, 40U) ==
        kNoAgentConnection);
    assert(router.pending_handle() == 22);
    assert(router.expire_unauthorized_pending(140U, 20U, 40U) == 22);
    assert(router.pending_handle() == kNoAgentConnection);
    assert(router.disconnecting_handle() == 22);
    assert(router.disconnected(22) == AgentConnectionRole::Disconnecting);

    assert(router.connected(23, 200U) == AgentConnectionAdmission::Pending);
    assert(!router.note_pending_activity(99, 210U));
    assert(router.note_pending_activity(23, 219U));
    assert(router.expire_unauthorized_pending(220U, 20U, 40U) ==
           kNoAgentConnection);
    assert(router.note_pending_activity(23, 239U));
    assert(router.expire_unauthorized_pending(240U, 20U, 40U) == 23);
    assert(router.pending_handle() == kNoAgentConnection);
    assert(router.disconnecting_handle() == 23);
    assert(router.disconnected(23) == AgentConnectionRole::Disconnecting);

    assert(router.connected(24, 300U) == AgentConnectionAdmission::Pending);
    assert(router.reject_pending(24));
    assert(router.pending_handle() == kNoAgentConnection);
    assert(router.disconnecting_handle() == 24);
    assert(!router.can_access(24, AgentGattSurface::Pairing));
    assert(router.disconnected(24) == AgentConnectionRole::Disconnecting);
    assert(!router.has_connections());
}
