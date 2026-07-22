#include "main/runtime/assistant/assistant_output_sync_policy.h"

#include <cassert>

int main()
{
    AssistantOutputSyncPolicy policy;

    policy.request(35);
    assert(!policy.begin(1000, false));
    const auto first = policy.begin(1100, true);
    assert(first);
    assert(first->volume == 35);

    policy.markVolumeResult(first->generation, true);
    policy.markMuteResult(first->generation, false);
    policy.update(1110);
    assert(policy.pendingVolume() == 35);
    assert(!policy.begin(1200, true));

    const auto retry = policy.begin(1360, true);
    assert(retry);
    assert(retry->volume == 35);
    assert(retry->generation != first->generation);

    policy.markMuteResult(first->generation, true);
    policy.request(70);
    policy.markVolumeResult(retry->generation, true);
    policy.markMuteResult(retry->generation, true);
    policy.update(1370);
    assert(policy.pendingVolume() == 70);

    const auto latest = policy.begin(1370, true);
    assert(latest);
    assert(latest->volume == 70);
    policy.markVolumeResult(latest->generation, true);
    policy.markMuteResult(latest->generation, true);
    policy.update(1380);
    assert(policy.pendingVolume() == -1);

    policy.request(120);
    const auto clamped = policy.begin(1400, true);
    assert(clamped);
    assert(clamped->volume == 100);
    policy.update(AssistantOutputSyncPolicy::CompletionTimeoutMs + 1400);
    assert(policy.pendingVolume() == 100);
    assert(!policy.begin(AssistantOutputSyncPolicy::CompletionTimeoutMs + 1500, true));
    assert(policy.begin(AssistantOutputSyncPolicy::CompletionTimeoutMs + 1660, true));

    policy.reset();
    assert(policy.pendingVolume() == -1);
    assert(!policy.inFlight());
}
