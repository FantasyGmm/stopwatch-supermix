#include "voice_audio_bridge.h"
#include "voice_audio_ring.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <type_traits>

static_assert(std::is_same_v<
              decltype(&swva_find_device_uid),
              int (*)(const char*, char*, size_t)>);
static_assert(std::is_same_v<
              decltype(&swva_get_default_input_uid),
              int (*)(char*, size_t)>);
static_assert(std::is_same_v<
              decltype(&swva_set_default_input_uid),
              int (*)(const char*)>);
static_assert(std::is_same_v<decltype(&swva_open), int (*)(const char*)>);
static_assert(std::is_same_v<decltype(&swva_start), int (*)(void)>);
static_assert(std::is_same_v<
              decltype(&swva_write_mono_16k),
              size_t (*)(const int16_t*, size_t)>);
static_assert(std::is_same_v<decltype(&swva_dropped_frames), uint64_t (*)(void)>);
static_assert(std::is_same_v<decltype(&swva_stop), void (*)(void)>);
static_assert(std::is_same_v<decltype(&swva_close), void (*)(void)>);

int main()
{
    VoiceAudioRing<24> ring;
    const int16_t mono[] = {100, -200};
    assert(ring.writeMono16kAsStereo48k(mono, 2) == 6);

    int16_t output[12] = {};
    assert(ring.readStereo(output, 6) == 6);
    const int16_t expected[] = {
        100, 100, 100, 100, 100, 100,
        -200, -200, -200, -200, -200, -200,
    };
    assert(std::equal(std::begin(output), std::end(output), std::begin(expected)));

    assert(ring.readStereo(output, 1) == 0);

    VoiceAudioRing<6> bounded_ring;
    const int16_t overflow[] = {1, 2, 3};
    assert(bounded_ring.writeMono16kAsStereo48k(overflow, 3) == 6);
    assert(bounded_ring.queuedFrames() == 6);
    assert(bounded_ring.droppedFrames() == 3);

    bounded_ring.clear();
    assert(bounded_ring.queuedFrames() == 0);
    return 0;
}
