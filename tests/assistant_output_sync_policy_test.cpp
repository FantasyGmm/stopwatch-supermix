#include "main/runtime/assistant/assistant_output_sync_policy.h"

#include <cassert>
#include <stdexcept>

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
    assert(policy.inFlight());
    assert(policy.takeTimeoutNotice());
    assert(!policy.takeTimeoutNotice());
    assert(!policy.begin(AssistantOutputSyncPolicy::CompletionTimeoutMs + 100000, true));
    policy.markVolumeResult(clamped->generation, true);
    policy.markMuteResult(clamped->generation, true);
    policy.update(AssistantOutputSyncPolicy::CompletionTimeoutMs + 100010);
    assert(policy.pendingVolume() == -1);
    assert(!policy.inFlight());

    policy.reset();
    policy.request(45);
    const auto exception_attempt = policy.begin(5000, true);
    assert(exception_attempt);
    bool mute_submission_attempted = false;
    const auto exception_result = SubmitAssistantOutputSyncPair(
        policy, exception_attempt->generation,
        []() -> bool { throw std::runtime_error("volume submit failed"); },
        [&mute_submission_attempted]() {
            mute_submission_attempted = true;
            return true;
        });
    assert(exception_result.volume_threw);
    assert(!exception_result.volume_submitted);
    assert(exception_result.mute_submitted);
    assert(mute_submission_attempted);
    policy.markMuteResult(exception_attempt->generation, true);
    policy.update(5010);
    assert(policy.pendingVolume() == 45);
    assert(policy.inFlight());
    policy.update(5000 + AssistantOutputSyncPolicy::CompletionTimeoutMs);
    assert(policy.takeTimeoutNotice());
    assert(!policy.begin(10000, true));

    policy.reset();
    policy.request(55);
    const auto second_attempt = policy.begin(11000, true);
    assert(second_attempt);
    bool second_submission_attempted = false;
    const auto double_failure = SubmitAssistantOutputSyncPair(
        policy, second_attempt->generation,
        []() { return false; },
        [&second_submission_attempted]() {
            second_submission_attempted = true;
            return false;
        });
    assert(!double_failure.volume_submitted);
    assert(!double_failure.volume_threw);
    assert(!double_failure.mute_submitted);
    assert(!double_failure.mute_threw);
    assert(second_submission_attempted);
    policy.update(11010);
    assert(policy.pendingVolume() == 55);
    assert(!policy.inFlight());
    assert(!policy.begin(11200, true));
    assert(policy.begin(11260, true));

    policy.reset();
    assert(policy.pendingVolume() == -1);
    assert(!policy.inFlight());
}
