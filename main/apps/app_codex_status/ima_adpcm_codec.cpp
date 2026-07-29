#include "ima_adpcm_codec.h"

#include <limits>


namespace ble_voice_probe {
namespace {

constexpr int kIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

constexpr int kStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767,
};

int clamp_predictor(int predictor)
{
    if (predictor < -32768) {
        return -32768;
    }
    if (predictor > 32767) {
        return 32767;
    }
    return predictor;
}

int clamp_index(int index)
{
    if (index < 0) {
        return 0;
    }
    if (index > 88) {
        return 88;
    }
    return index;
}

uint8_t encode_nibble(int16_t sample, int& predictor, int& index)
{
    const int step = kStepTable[index];
    int difference = static_cast<int>(sample) - predictor;
    uint8_t nibble = 0;
    if (difference < 0) {
        nibble = 8;
        difference = -difference;
    }

    int delta = step >> 3;
    if (difference >= step) {
        nibble |= 4;
        difference -= step;
        delta += step;
    }
    if (difference >= (step >> 1)) {
        nibble |= 2;
        difference -= step >> 1;
        delta += step >> 1;
    }
    if (difference >= (step >> 2)) {
        nibble |= 1;
        delta += step >> 2;
    }

    if ((nibble & 8) != 0) {
        predictor -= delta;
    } else {
        predictor += delta;
    }
    predictor = clamp_predictor(predictor);
    index = clamp_index(index + kIndexTable[nibble]);
    return nibble;
}

}  // namespace

size_t ima_adpcm_block_size(size_t sample_count)
{
    if (sample_count == 0 || sample_count > std::numeric_limits<size_t>::max() - 4) {
        return 0;
    }
    return 4 + sample_count / 2;
}

size_t encode_ima_adpcm_block(const int16_t* samples, size_t sample_count,
                              uint8_t* output, size_t capacity)
{
    const size_t required = ima_adpcm_block_size(sample_count);
    if (required == 0 || samples == nullptr || output == nullptr || capacity < required) {
        return 0;
    }

    const uint16_t first_sample = static_cast<uint16_t>(samples[0]);
    output[0] = static_cast<uint8_t>(first_sample & 0xff);
    output[1] = static_cast<uint8_t>(first_sample >> 8);
    output[2] = 0;
    output[3] = 0;

    int predictor = static_cast<int>(samples[0]);
    int index = 0;
    for (size_t sample_index = 1; sample_index < sample_count; ++sample_index) {
        const uint8_t nibble = encode_nibble(samples[sample_index], predictor, index);
        const size_t output_index = 4 + (sample_index - 1) / 2;
        if ((sample_index & 1) != 0) {
            output[output_index] = nibble;
        } else {
            output[output_index] |= static_cast<uint8_t>(nibble << 4);
        }
    }
    return required;
}

}  // namespace ble_voice_probe
