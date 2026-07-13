#include "runtime/resource_supervisor.h"
#include <cassert>

int main()
{
    ResourceSupervisor resources;

    assert(resources.acquireWifi(RuntimeOwner::Assistant) == AcquireResult::Granted);
    assert(resources.acquireWifi(RuntimeOwner::Codex) == AcquireResult::Granted);
    assert(resources.acquireWifi(RuntimeOwner::Codex) == AcquireResult::AlreadyOwned);
    assert(resources.wifiLeaseCount() == 2);

    assert(resources.acquireCapture(RuntimeOwner::Assistant) == AcquireResult::Granted);
    assert(resources.acquireCapture(RuntimeOwner::Assistant) == AcquireResult::AlreadyOwned);
    assert(resources.acquireCapture(RuntimeOwner::Codex) == AcquireResult::Busy);
    assert(resources.releaseCapture(RuntimeOwner::Codex) == ReleaseResult::NotOwned);
    assert(resources.releaseCapture(RuntimeOwner::Assistant) == ReleaseResult::Released);
    assert(resources.releaseCapture(RuntimeOwner::Assistant) == ReleaseResult::NotOwned);
    assert(resources.acquireCapture(RuntimeOwner::Codex) == AcquireResult::Granted);

    assert(resources.acquirePlayback(RuntimeOwner::Assistant) == AcquireResult::Granted);
    assert(resources.acquirePlayback(RuntimeOwner::Codex) == AcquireResult::Busy);
    assert(resources.releasePlayback(RuntimeOwner::Assistant) == ReleaseResult::Released);
    assert(resources.acquirePlayback(RuntimeOwner::Codex) == AcquireResult::Granted);

    assert(resources.acquireWifiAp(RuntimeOwner::Badge) == AcquireResult::Busy);
    assert(resources.releaseWifi(RuntimeOwner::Assistant) == ReleaseResult::Released);
    assert(resources.wifiLeaseCount() == 1);
    assert(resources.releaseWifi(RuntimeOwner::Codex) == ReleaseResult::LastOwnerReleased);
    assert(resources.releaseWifi(RuntimeOwner::Codex) == ReleaseResult::NotOwned);
    assert(resources.acquireWifiAp(RuntimeOwner::Badge) == AcquireResult::Granted);
    assert(resources.acquireWifi(RuntimeOwner::Assistant) == AcquireResult::Busy);
    assert(resources.acquireWifiAp(RuntimeOwner::Badge) == AcquireResult::AlreadyOwned);
    assert(resources.releaseWifiAp(RuntimeOwner::Assistant) == ReleaseResult::NotOwned);
    assert(resources.releaseWifiAp(RuntimeOwner::Badge) == ReleaseResult::LastOwnerReleased);

    resources.block();
    assert(resources.acquireWifi(RuntimeOwner::Assistant) == AcquireResult::Blocked);
    assert(resources.acquireCapture(RuntimeOwner::Assistant) == AcquireResult::Blocked);
    assert(resources.acquirePlayback(RuntimeOwner::Codex) == AcquireResult::AlreadyOwned);
    assert(resources.releaseCapture(RuntimeOwner::Codex) == ReleaseResult::Released);
    assert(resources.releasePlayback(RuntimeOwner::Codex) == ReleaseResult::Released);

    const ResourceSnapshot blocked_snapshot = resources.snapshot();
    assert(blocked_snapshot.capture_owner == RuntimeOwner::None);
    assert(blocked_snapshot.playback_owner == RuntimeOwner::None);
    assert(blocked_snapshot.wifi_lease_count == 0);
    assert(!blocked_snapshot.wifi_ap_active);
    assert(blocked_snapshot.blocked);

    resources.unblock();
    assert(resources.acquireCapture(RuntimeOwner::Assistant) == AcquireResult::Granted);
    const ResourceSnapshot ready_snapshot = resources.snapshot();
    assert(ready_snapshot.capture_owner == RuntimeOwner::Assistant);
    assert(!ready_snapshot.blocked);

    return 0;
}
