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
    assert(encode_ima_adpcm_block(nullptr, 320, encoded.data(), encoded.size()) == 0);
    assert(encode_ima_adpcm_block(step.data(), step.size(), encoded.data(), 4) == 0);
}
