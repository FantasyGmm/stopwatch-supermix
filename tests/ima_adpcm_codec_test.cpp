#include "main/apps/app_codex_status/ima_adpcm_codec.h"

#include <array>
#include <cassert>

int main()
{
    using namespace ble_voice_probe;

    const std::array<int16_t, 5> zero = {0, 0, 0, 0, 0};
    std::array<uint8_t, 16> encoded = {};
    size_t length = encode_ima_adpcm_block(
        zero.data(), zero.size(), encoded.data(), encoded.size());
    assert(length == 6);
    assert(encoded[0] == 0 && encoded[1] == 0);
    assert(encoded[2] == 0 && encoded[3] == 0);
    assert(encoded[4] == 0 && encoded[5] == 0);

    const std::array<int16_t, 2> step = {0, 7};
    length = encode_ima_adpcm_block(
        step.data(), step.size(), encoded.data(), encoded.size());
    assert(length == 5);
    assert(encoded[4] == 0x04);

    assert(ima_adpcm_block_size(320) == 164);
    assert(ima_adpcm_block_size(0) == 0);

    std::array<uint8_t, 16> sentinel = {};
    sentinel.fill(0xa5);
    std::array<uint8_t, 16> rejected = sentinel;
    assert(encode_ima_adpcm_block(zero.data(), 0, rejected.data(), rejected.size()) == 0);
    assert(rejected == sentinel);

    rejected = sentinel;
    assert(encode_ima_adpcm_block(step.data(), step.size(), nullptr, rejected.size()) == 0);
    assert(rejected == sentinel);

    rejected = sentinel;
    assert(encode_ima_adpcm_block(step.data(), step.size(), rejected.data(), 0) == 0);
    assert(rejected == sentinel);

    rejected = sentinel;
    assert(encode_ima_adpcm_block(nullptr, 320, rejected.data(), rejected.size()) == 0);
    assert(rejected == sentinel);

    rejected = sentinel;
    assert(encode_ima_adpcm_block(step.data(), step.size(), rejected.data(), 4) == 0);
    assert(rejected == sentinel);
}
