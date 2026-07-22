#include "runtime/services/audio_jitter_buffer_policy.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace {

constexpr std::size_t FrameBytes = 5292;
constexpr std::size_t StartBufferBytes = FrameBytes * 3;
constexpr std::int64_t MaxBufferWaitUs = 220000;

void waitsForThreeFramesBeforeStarting()
{
    AudioJitterBufferPolicy policy(StartBufferBytes, MaxBufferWaitUs);

    policy.onQueued(FrameBytes, 1000);
    assert(!policy.readyToPlay(1000));
    policy.onQueued(FrameBytes, 2000);
    assert(!policy.readyToPlay(2000));
    policy.onQueued(FrameBytes, 3000);
    assert(policy.readyToPlay(3000));
}

void releasesShortAudioAfterMaximumWait()
{
    AudioJitterBufferPolicy policy(StartBufferBytes, MaxBufferWaitUs);

    policy.onQueued(FrameBytes, 1000);
    assert(!policy.readyToPlay(220999));
    assert(policy.readyToPlay(221000));
}

void rearmsBufferingAfterQueueDrains()
{
    AudioJitterBufferPolicy policy(StartBufferBytes, MaxBufferWaitUs);

    policy.onQueued(StartBufferBytes, 1000);
    assert(policy.readyToPlay(1000));
    policy.onConsumed(StartBufferBytes);

    policy.onQueued(FrameBytes, 2000);
    assert(!policy.readyToPlay(2000));
}

}  // namespace

int main()
{
    waitsForThreeFramesBeforeStarting();
    releasesShortAudioAfterMaximumWait();
    rearmsBufferingAfterQueueDrains();
    return 0;
}
